#ifndef NET_H
#define NET_H

void fd_nonblocking(int fd);
int init_listeners(int epoll_fd, struct configs_s *configs);
int accept_new_client(int epoll_fd, int fd);
int init_connect(int epoll_fd, struct shared_data_s *shared);
int init_bind();
int init_udpa();

#endif