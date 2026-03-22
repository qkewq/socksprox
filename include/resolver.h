#ifndef RESOLVER_H
#define RESOLVER_H

typedef struct Shared Shared;
typedef struct Configs Configs;

typedef struct Resolver{
	int rin_pipe;
	int win_pipe;
	int rout_pipe;
	int wout_pipe;
} Resolver;

extern Resolver resolver;

int resolve_ret(int epoll_fd, Configs *configs);
int init_resolver(pthread_t *resolver_thread, int epoll_fd);
void resolve(Shared *shared, char *input); // The len,name,port from the SOCKS req

#endif
