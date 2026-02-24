#ifndef EPOLL_DATA_H
#define EPOLL_DATA_H

#define OUTBUFFSIZE 4096

#define TYPE_ISLISTENER 0x00
#define TYPE_NOLISTENER 0x01

#define STATE_STATELESS         0x00
#define STATE_LISTENING         0x01
#define STATE_WAITINGMETHODS    0x02
#define STATE_SENDINGMETHOD     0x03
#define STATE_AUTHENTICATING    0x04
#define STATE_WAITINGCOMMAND    0x05
#define STATE_CONNECTING        0x06
#define STATE_BINDLISTENING     0x07
#define STATE_SENDINGREPLY      0x08
#define STATE_SENDINGREPLY_2    0x09
#define STATE_HALF              0x0A
#define STATE_FULL              0x0B
#define STATE_HALFCLOSE         0x0C

struct method_s{
    uint8_t method;
};

struct req_info_s{
    uint8_t rep;
    uint8_t cmd;
    int ai_index;
    struct addrinfo *ai;
    struct sockaddr *sa;
}

struct ringbuff_s{
    uint8_t *buff;
    size_t readhead;
    size_t writehead;
    size_t capacity;
    size_t used;
};

struct shared_data_s{
    int clientfd;
    uint8_t cfd_state;
    int serverfd;
    uint8_t sfd_state;
    struct ringbuff_s c_outbuff;
    struct ringbuff_s s_outbuff; //Will act as inbuff during handshake
    void *ptr;
};

struct epoll_data_s{
    uint8_t is_listener;
    int self_fd;
    struct shared_data_s *shared;
};

size_t write_ringbuff(struct ringbuff_s *dst, uint8_t *src, size_t srclen);
size_t peek_ringbuff(struct ringbuff_s *src, uint8_t *dst, size_t dstlen);
size_t consume_ringbuff(struct ringbuff_s *src, size_t consume);
int ep_add_listener(int epoll_fd, int fd);
int ep_add_new_client(int epoll_fd, int fd, struct epoll_data_s *data);
int ep_connecting(int epoll_fd, int fd, struct epoll_data_s *data);
int ep_waiting_send(int epoll_fd, int fd, void *data);
int ep_done_sending(int epoll_fd, int fd, void *data);

#endif