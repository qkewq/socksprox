#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include "epoll_data.h"
#include "socks.h"
#include "logger.h"
#include "resolver.h"

size_t write_ringbuff(Ringbuff *dst, uint8_t *src, size_t srclen){
	size_t free = dst->capacity - dst->used;
	if(srclen > free){ // Not enough space, no partial writes
		return 0;
	}
	size_t first_write = dst->capacity - dst->writehead;
	if(first_write > srclen){
		first_write = srclen;
	}
	memcpy(dst->buff + dst->writehead, src, first_write);
	size_t second_write = srclen - first_write;
	if(second_write > 0){
		memcpy(dst->buff, src + first_write, second_write);
	}
	dst->writehead  = (dst->writehead + srclen) % dst->capacity;
	dst->used += srclen;

	return srclen;
}

size_t peek_ringbuff(Ringbuff *src, uint8_t *dst, size_t dstlen){
	if(src->used == 0){
		return 0;
	}
	if(src->used < dstlen){
		dstlen = src->used;
	}
	size_t first_read = src->capacity - src->readhead;
	if(first_read > dstlen){
		first_read = dstlen;
	}
	memcpy(dst, src->buff + src->readhead, first_read);
	size_t second_read = dstlen - first_read;
	if(second_read > 0){
		memcpy(dst + first_read, src->buff, second_read);
	}

	return dstlen;
}

size_t consume_ringbuff(struct Ringbuff *src, size_t consume){
	if(src->used == 0){
		return 0;
	}
	if(consume > src->used){
		consume = src->used;
	}
	src->readhead = (src->readhead + consume) % src->capacity;
	src->used -= consume;

	return consume;
}

int ep_add_listener(int epoll_fd, int fd){
	struct epoll_event ev;
	Data *data = malloc(sizeof(Data));
	if(data == NULL){
		log_error(L_MALLOCERROR);
		return -1;
	}
	memset(&ev, 0, sizeof(ev));
	memset(data, 0, sizeof(Data));

	data->type = TYPE_LISTENER;
	data->self_fd = fd;
	data->shared = NULL;
	ev.events = EPOLLIN;
	ev.data.ptr = data;

	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
		log_error(L_EPOLLERRORADD);
		free(data);
		return -1;
	}

	return 0;
}

int ep_add_resolver(int epoll_fd){
	struct epoll_event ev;
	Data *data = malloc(sizeof(Data));
	if(!data){
		log_error(L_MALLOCERROR);
		return -1;
	}
	memset(&ev, 0, sizeof(ev));
	memset(data, 0, sizeof(Data));

	data->type = TYPE_RESOLVER;
	data->self_fd  = resolver.rout_pipe;
	data->shared = NULL;
	ev.events = EPOLLIN;
	ev.data.ptr = data;

	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, resolver.rout_pipe, &ev) == -1){
		return -1;
	}

	return 0;

}

int ep_add_new_client(int epoll_fd, int fd, Data *data){
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = data;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
		log_error(L_EPOLLERRORADD);
		return -1;
	}

	return 0;
}

int ep_connecting(int epoll_fd, int fd, void *data){
	struct epoll_event ev;
	ev.events = EPOLLOUT;
	ev.data.ptr = data;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
		log_error(L_EPOLLERRORADD);
		return -1;
	}

	return 0;
}

int ep_waiting_send(int epoll_fd, int fd, void *data){
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLOUT;
	ev.data.ptr = data;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1){
		log_error(L_EPOLLERRORMOD);
		return -1;
	}

	return 0;
}

int ep_done_sending(int epoll_fd, int fd, void *data){
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.ptr = data;
	if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1){
		log_error(L_EPOLLERRORMOD);
		return -1;
	}

	return 0;
}

int ep_delete_fd(int epoll_fd, int fd){
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);

	return 0;
}

void free_data(Data *data){
	if(data->shared->info){
		if(data->shared->info->sa){
			free(data->shared->info->sa);
		}
		if(data->shared->info->ai){
			freeaddrinfo(data->shared->info->ai);
		}
		free(data->shared->info);
		data->shared->info = NULL;
	}
	if(data->shared->ptr){
		free(data->shared->ptr);
		data->shared->ptr = NULL;
	}
	free(data->shared->client_out.buff);
	free(data->shared->server_out.buff);
	data->shared->client_out.buff = NULL;
	data->shared->server_out.buff = NULL;

	if(data->self_fd == data->shared->client_fd){
		data->shared->client_data = NULL;
	}
	if(data->self_fd == data->shared->server_fd){
		data->shared->server_data = NULL;
	}
	if(!data->shared->client_data && !data->shared->server_data){
		free(data->shared);
	}
	free(data);
}
