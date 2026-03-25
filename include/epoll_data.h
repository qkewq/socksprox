#ifndef EPOLL_DATA_H
#define EPOLL_DATA_H

#define OUTBUFFSIZE 4096

#define TYPE_LISTENER   0x00
#define TYPE_PEER       0x01
#define TYPE_RESOLVER   0x02

#define STATE_NO_STATE          0x00
#define STATE_WAITING_METHODS   0x01
#define STATE_SENDING_METHOD    0x02
#define STATE_AUTHENTICATING    0x03
#define STATE_WAITING_COMMAND   0x04
#define STATE_ASYNC_DNS         0x05
#define STATE_SENDING_REPLY     0x06
#define STATE_SENDING_REPLY_2   0x07
#define STATE_CONNECTING        0x08
#define STATE_BIND_LISTENING    0x09
#define STATE_FULL              0x0A
#define STATE_FULL_UDPA         0x0B
#define STATE_HALF_CLOSE        0x0C
#define STATE_CLOSED            0x0D

#define FLAG_READ_CLOSED        0x01
#define FLAG_WRITE_CLOSED       0x02
#define FLAG_FLUSHING           0x04

typedef struct Info{
	uint8_t method;
	uint8_t rep;
	uint8_t cmd;
	uint8_t atyp;
	int resolver_ret;
	int ai_index;
	struct addrinfo *ai;
	struct sockaddr *sa;
} Info;

typedef struct Ringbuff{
	uint8_t *buff;
	size_t readhead;
	size_t writehead;
	size_t capacity;
	size_t used;
} Ringbuff;

typedef struct Shared{
	uint8_t state;
	int client_fd; // Or UDP Associate control connection
	int server_fd; // Or UDP Relay port
	struct Ringbuff client_out;
	struct Ringbuff server_out; //Will act as inbuff during handshake
	struct Data *client_data;
	struct Data *server_data;
	uint8_t client_flags;
	uint8_t server_flags;
	struct Info *info;
	void *ptr;
} Shared;

typedef struct Data{
	uint8_t type;
	int self_fd;
	struct Shared *shared;
} Data;

size_t write_ringbuff(struct Ringbuff *dst, uint8_t *src, size_t srclen);
size_t peek_ringbuff(struct Ringbuff *src, uint8_t *dst, size_t dstlen);
size_t consume_ringbuff(struct Ringbuff *src, size_t consume);
int ep_add_listener(int epoll_fd, int fd);
int ep_add_resolver(int epoll_fd);
int ep_add_new_client(int epoll_fd, int fd, Data *data);
int ep_connecting(int epoll_fd, int fd, void *data);
int ep_waiting_send(int epoll_fd, int fd, void *data);
int ep_done_sending(int epoll_fd, int fd, void *data);
int ep_delete_fd(int epoll_fd, int fd);
void free_data(Data *data);

#endif
