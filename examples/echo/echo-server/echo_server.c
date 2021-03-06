#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>
#include <assert.h>
#include <limits.h>
#include <errno.h>

#include <mtcp_api.h>
#include <netlib.h>
#include <cpu.h>

#define BUFFER_SIZE 1024
#define ECHO_PORT 6999
#define on_error(...) { fprintf(stderr, __VA_ARGS__); fflush(stderr); exit(1); }

static int num_cores;
static int core_limit;
static int do_shutdown = 0;

void 
SignalHandler(int signum)
{
	do_shutdown = 1;
}

int main (int argc, char *argv[]) {
  struct mtcp_conf mcfg;
  char *conf_file;
  
  mctx_t mctx;
  int server_fd, client_fd, err, o;
  struct sockaddr_in server, client;
  char buf[BUFFER_SIZE];

  num_cores = GetNumCPUs();
  core_limit = num_cores;  

  while (-1 != (o = getopt(argc, argv, "N:f:"))) {
    switch(o) {
    case 'N':
      core_limit = mystrtol(optarg, 10);
      if (core_limit > num_cores) {
        on_error("CPU limit should be smaller than the "
               "number of CPUS: %d\n", num_cores);
        return -1;
      } else if (core_limit < 1) {
        on_error("CPU limit should be greater than 0\n");
        return -1;
      }
      /** 
       * it is important that core limit is set 
       * before mtcp_init() is called. You can
       * not set core_limit after mtcp_init()
       */
      mtcp_getconf(&mcfg);
      mcfg.num_cores = core_limit;
      mtcp_setconf(&mcfg);
      break;
    case 'f':
      conf_file = optarg;
      break;
    }
  } 

  if (conf_file == NULL) {
    on_error("mTCP configuration file is not set!\n");
    exit(-1);
  }
  
  err = mtcp_init(conf_file);
  if (err) {
    on_error("Failed to initialize mtcp.\n");
    exit(-1);
  }

	mtcp_register_signal(SIGINT, SignalHandler);

  mtcp_core_affinitize(0);
  mctx = mtcp_create_context(0);
  server_fd = mtcp_socket(mctx, AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) 
    on_error("Could not create socket\n");

  server.sin_family = AF_INET;
  server.sin_port = htons(ECHO_PORT);
  server.sin_addr.s_addr = htonl(INADDR_ANY);

  err = mtcp_bind(mctx, server_fd, (struct sockaddr *) &server, sizeof(server));
  if (err < 0) 
    on_error("Could not bind socket\n");

  err = mtcp_listen(mctx, server_fd, 128);
  if (err < 0) 
    on_error("Could not listen on socket\n");

  printf("Server is listening on %d\n", ECHO_PORT);

  while (1) {
    socklen_t client_len = sizeof(client);
    int read, write;

new_accept:    
    client_fd = mtcp_accept(mctx, server_fd, (struct sockaddr *) &client, &client_len);

    if (client_fd < 0) 
      on_error("Could not establish new connection\n");

		printf("new connection fd %d comming\n", client_fd);
    mtcp_setsock_nonblock(mctx, client_fd);
    while (1) {
			if(do_shutdown)
				break;

			while(1) {
				if(do_shutdown)
					break;

	      read = mtcp_recv(mctx, client_fd, buf, BUFFER_SIZE, 0);
	      if(read == 0) {
	        printf("Client read failed\n");
	        goto new_accept;
	      } else if (read < 0 && errno == EAGAIN) {
					continue;
				} else {
				  break;
				}
			}

			while(1) {
				if(do_shutdown)
					break;

		    write = mtcp_write(mctx, client_fd, buf, (size_t)read);
		    if (write == 0) {
		      printf("Client write failed\n");
	        goto new_accept;
				} else if (write < 0 && errno == EAGAIN) {
					continue;
				} else {
				  break;
				}
			}
    }
  }

  return 0;
}
