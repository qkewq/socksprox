#ifndef NET_H
#define NET_H

void fd_nonblocking(int fd);
int init_listeners(int epoll_fd, struct configs_s *configs);
int accept_new_client(int epoll_fd, int fd);

#endif