#ifndef NET_H
#define NET_H

struct configs_s;
struct shared_data_s;
struct epoll_data_s;

void fd_nonblocking(int fd);
int init_listeners(int epoll_fd, struct configs_s *configs);
int accept_new_client(int epoll_fd, int fd);
int init_connect(int epoll_fd, struct shared_data_s *shared);
int init_bind();
int init_udpa();
int recv_eof(struct epoll_data_s *data);
int read_data(int self_fd, struct epoll_data_s *data);
int send_data(int self_fd, struct epoll_data_s *data);

#endif