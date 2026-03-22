#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include "resolver.h"
#include "epoll_data.h"
#include "socks.h"
#include "config.h"

Resolver resolver = {0}; // Global extern declaration

int resolve_ret(int epoll_fd, Configs *configs){ // Extremely shady function
	int gai_ret = -1;
	Shared *shared = NULL;
	if(read(resolver.rout_pipe, &gai_ret, sizeof(gai_ret)) < sizeof(gai_ret)){
		return 0;
	}
	if(read(resolver.rout_pipe, &shared, sizeof(shared)) < sizeof(shared)){
		return 0;
	}

	shared->info->resolver_ret = gai_ret;
	shared->state = STATE_WAITING_COMMAND;
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = shared->client_data;
	socks5(epoll_fd, &event, configs); // Shady workaround

	return 0;
}

void *resolver_th(void *arg){
	struct pollfd pfd;
	pfd.fd = resolver.rin_pipe;
	pfd.events = POLLIN;
	int ready = 0;
	while(1){
		ready = poll(&pfd, 1, -1);
		if(pfd.revents & POLLIN){
			Shared *shared = NULL;
			if(read(pfd.fd, &shared, sizeof(shared)) != sizeof(shared)){
				continue;
			}
			if(!shared){
				pthread_exit(NULL);
			}
			uint8_t name_len = 0;
			if(read(pfd.fd, &name_len, sizeof(name_len)) != sizeof(name_len)){
				continue;
			}
			char name[256] = {0};
			int r_len = read(pfd.fd, &name, sizeof(name));
			if(r_len != name_len){
				continue;
			}
			name[r_len + 1] = '\0';
			uint16_t port = 0;
			if(read(pfd.fd, &port, sizeof(port)) != sizeof(port)){
				continue;
			}

			struct addrinfo hints;
			struct addrinfo *res = NULL;
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;

			char c_port[6] = {0};
			snprintf(c_port, sizeof(c_port), "%d", htons(port));

			int gai_ret = getaddrinfo(name, c_port, &hints, &res);
			shared->info->ai = res;

			uint8_t msg[sizeof(int) + sizeof(shared)] = {0};
			memcpy(&msg[0], &gai_ret, sizeof(int));
			memcpy(&msg[sizeof(int)], &shared, sizeof(shared));
			write(resolver.wout_pipe, &msg, sizeof(msg));
		}
	}
}

int init_resolver(pthread_t *thread, int epoll_fd){
	int inpipe[2];
	if(pipe2(inpipe, O_NONBLOCK) == -1){
		return -1;
	}

	resolver.rin_pipe = inpipe[0];
	resolver.win_pipe = inpipe[1];

	int outpipe[2];
	if(pipe2(outpipe, (O_DIRECT | O_NONBLOCK)) == -1){
		return -1;
	}

	resolver.rout_pipe = outpipe[0];
	resolver.wout_pipe = outpipe[1];

	if(ep_add_resolver(epoll_fd) == -1){
		return -1;
	}

	return pthread_create(thread, NULL, resolver_th, NULL);
}

void resolve(Shared *shared, char *input){
	shared->state = STATE_ASYNC_DNS;
	int name_len = 3 + input[0]; // 3 bytes fixed + variable
	uint8_t msg[sizeof(shared) + name_len];
	memcpy(msg, &shared, sizeof(shared));
	memcpy(&msg[1 + sizeof(shared)], input, name_len);
	write(resolver.win_pipe, &msg, sizeof(msg));
}
