#ifndef EPOLL_DATA_H
#define EPOLL_DATA_H

#define TYPE_ISLISTENER 0x00
#define TYPE_NOLISTENER 0x01

#define STATE_STATELESS 0x00
#define STATE_LISTENING 0x01
#define STATE_CACCEPTED 0x02
#define STATE_AUTHENTNG 0x03
#define STATE_AUTHENTED 0x04
#define STATE_CONNECTIN 0x05
#define STATE_BNDLISTNG 0x06
#define STATE_HALF      0x07
#define STATE_FULL      0x08

struct epoll_data_s{
    uint8_t is_listener;
    int clientfd;
    uint8_t cfd_state;
    int serverfd;
    uint8_t sfd_state;
    void *ptr;
};

struct listener_s{
    uint8_t is_listener;
    int listen_fd;
};

int ep_add_listener(int epoll_fd, int fd);

#endif