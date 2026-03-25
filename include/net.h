#ifndef NET_H
#define NET_H

typedef struct Configs Configs;
typedef struct Shared Shared;
typedef struct Data Data;
typedef struct Sockaddr_ll Sockaddr_ll;
typedef struct Ringbuff Ringbuff;

void fd_nonblocking(int fd);
int init_listeners(int epoll_fd, Sockaddr_ll *current);
int listen_err(int epoll_fd, uint32_t ev, Data *data);
int accept_new_client(int epoll_fd, int fd);
int init_connect(int epoll_fd, Shared *shared);
int init_bind(int epoll_fd, Shared *shared, Configs *configs);
int init_udpa(int epoll_fd, Shared *shared, Configs *configs);
int send_traffic(int fd, Shared *shared);
int read_traffic(int fd, Shared *shared);

#endif