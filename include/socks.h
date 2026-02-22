#ifndef SOCKS_H
#define SOCKS_H

struct epoll_data_s;
struct configs_s;
uint16_t socks5(int epoll_fd, struct epoll_event *event, struct configs_s *configs);

#endif