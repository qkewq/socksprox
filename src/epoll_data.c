#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include "epoll_data.h"

int ep_add_listener(int epoll_fd, int fd){
    struct epoll_event ev;
    struct listener_s *data = malloc(sizeof(struct listener_s));
    if(data == NULL){
        return -1;
    }
    memset(&ev, 0, sizeof(ev));
    memset(data, 0, sizeof(struct listener_s));

    data->is_listener = TYPE_ISLISTENER;
    data->listen_fd = fd;
    ev.event = EPOLLIN;
    ev.data.ptr = data;

    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1){
        free(data);
        return -1;
    }

    return 0;
}