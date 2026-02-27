#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include "epoll_data.h"

size_t write_ringbuff(struct ringbuff_s *dst, uint8_t *src, size_t srclen){
    if(!dst || !dst->buff || !src){
        return 0;
    }
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

size_t peek_ringbuff(struct ringbuff_s *src, uint8_t *dst, size_t dstlen){
    if(!src || !src->buff || !dst || src->used == 0){
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

size_t consume_ringbuff(struct ringbuff_s *src, size_t consume){
    if(!src || !src->buff || src->used == 0){
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
    struct epoll_data_s *data = malloc(sizeof(struct epoll_data_s));
    if(data == NULL){
        return -1;
    }
    memset(&ev, 0, sizeof(ev));
    memset(data, 0, sizeof(struct epoll_data_s));

    data->is_listener = TYPE_ISLISTENER;
    data->self_fd = fd;
    data->shared = NULL;
    ev.events = EPOLLIN;
    ev.data.ptr = data;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
        free(data);
        return -1;
    }

    return 0;
}

int ep_add_new_client(int epoll_fd, int fd, struct epoll_data_s *data){
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = data;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
        return -1;
    }

    return 0;
}

int ep_connecting(int epoll_fd, int fd, struct epoll_data_s *data){
    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.ptr = data;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
        return -1;
    }

    return 0;
}

int ep_waiting_send(int epoll_fd, int fd, void *data){
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.ptr = data;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1){
        return -1;
    }

    return 0;
}

int ep_done_sending(int epoll_fd, int fd, void *data){
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = data;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == -1){
        return -1;
    }

    return 0;
}
