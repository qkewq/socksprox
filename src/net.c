#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#include "net.h"
#include "config.h"
#include "epoll_data.h"
#include "socks.h"

void fd_nonblocking(int fd){
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

int init_listeners(int epoll_fd, struct configs_s *configs){
    struct listen_addrs_s *current = configs->addrs;
    struct addrinfo *ai_current = NULL;
    if(current == NULL){
        return NOLISADDRS;
    }
    while(current != NULL){
        ai_current = current->addr;
        while(ai_current != NULL){
            int sfd = socket(ai_current->ai_family, ai_current->ai_socktype, ai_current->ai_protocol);
            if(sfd == -1){
                return SOCKOPEN_ERR;
            }
            if(ai_current->ai_family == AF_INET6){
                int flag = 1;
                if(setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &flag, sizeof(flag)) != 0){
                    return SETSOCKOPT_ERR;
                }
            }
            if(bind(sfd, ai_current->ai_addr, ai_current->ai_addrlen) == -1){
                return BIND_ERR;
            }
            fd_nonblocking(sfd);
            listen(sfd, SOMAXCONN);
            int ret = ep_add_listener(epoll_fd, sfd);
            if(ret != SUCCESS){
                return ret;
            }
            ai_current = ai_current->ai_next;
        }
        current = current->next;
    }

    return SUCCESS;
}

int accept_new_client(int epoll_fd, int fd){
    int new_fd = accept(fd, NULL, NULL);
    if(new_fd == -1){
        return ACCEPT_ERR;
    }
    fd_nonblocking(new_fd);
    struct epoll_data_s *data = malloc(sizeof(struct epoll_data_s));
    if(data == NULL){
        close(new_fd);
        return MALLOC_ERR;
    }
    struct shared_data_s *shared = malloc(sizeof(struct shared_data_s));
    if(shared == NULL){
        close(new_fd);
        free(data);
        return MALLOC_ERR;
    }
    memset(data, 0, sizeof(*data));
    memset(shared, 0, sizeof(*shared));
    data->is_listener = TYPE_NOLISTENER;
    data->self_fd = new_fd;
    data->shared = shared;
    shared->c_data = data;
    shared->s_data = NULL;
    shared->clientfd = new_fd;
    shared->cfd_state = STATE_WAITINGMETHODS;
    shared->serverfd = -1;
    shared->sfd_state = STATE_STATELESS;
    shared->c_outbuff.buff = malloc(OUTBUFFSIZE);
    shared->c_outbuff.capacity = OUTBUFFSIZE;
    shared->s_outbuff.buff = malloc(OUTBUFFSIZE);
    shared->s_outbuff.capacity = OUTBUFFSIZE;
    shared->ptr = NULL;
    if(shared->c_outbuff.buff == NULL ||
        shared->s_outbuff.buff == NULL){
        close(new_fd);
        free(shared->c_outbuff.buff);
        free(shared->s_outbuff.buff);
        free(shared);
        free(data);
        return MALLOC_ERR;
    }

    int ret = ep_add_new_client(epoll_fd, new_fd, data);
    if(ret != SUCCESS){
        close(new_fd);
        free(data);
        return ret;
    }

    return SUCCESS;
}

int init_connect(int epoll_fd, struct shared_data_s *shared){
    if(!shared || !shared->ptr){
        return NULLCHK_ERR;
    }
    struct req_info_s *info = shared->ptr;
    if(!info->ai && !info->sa){
        return NULLCHK_ERR;
    }
    if(info->rep != 0xFF){
        return GENERAL_ERR;
    }
    int sfd = -1;
    if(info->sa){
        if(info->sa->sa_family == AF_INET){
            sfd = socket(AF_INET, SOCK_STREAM, 0);
            if(sfd == -1){
                info->rep = REP_GENFAIL;
                return SUCCESS;
            }
            fd_nonblocking(sfd);
            if(connect(sfd, info->sa, sizeof(*info->sa)) == -1){
                if(errno != EAGAIN && errno != EINPROGRESS){
                    info->rep = REP_GENFAIL;
                    return SUCCESS;
                }
            }
        }
        else if(info->sa->sa_family == AF_INET6){
            sfd = socket(AF_INET6, SOCK_STREAM, 0);
            if(sfd == -1){
                info->rep == REP_GENFAIL;
                return SUCCESS;
            }
            fd_nonblocking(sfd);
            if(connect(sfd, info->sa, sizeof(*info->sa)) == -1){
                if(errno != EAGAIN && errno != EINPROGRESS){
                    info->rep = REP_GENFAIL;
                    return SUCCESS;
                }
            }
        }
    }
    else if(info->ai){
        sfd = socket(info->ai->ai_family, info->ai->ai_socktype, info->ai->ai_protocol);
        if(sfd == -1){
            info->rep = REP_GENFAIL;
            return SUCCESS;
        }
        fd_nonblocking(sfd);
        if(connect(sfd, info->ai->ai_addr, info->ai->ai_addrlen) == -1){
            if(errno != EAGAIN && errno != EINPROGRESS){
                info->rep = REP_GENFAIL;
                return SUCCESS;
            }
        }
        info->ai_index = 0;
    }
    struct epoll_data_s *data = malloc(sizeof(struct epoll_data_s));
    if(!data){
        info->rep = REP_GENFAIL;
        return SUCCESS; // Success so we can send reply to client before closing
    }
    data->is_listener = TYPE_NOLISTENER;
    data->self_fd = sfd;
    data->shared = shared;
    shared->s_data = data;
    shared->serverfd = sfd;
    shared->sfd_state = STATE_CONNECTING;
    consume_ringbuff(&shared->s_outbuff, OUTBUFFSIZE); // Clear temp inbuff
    if(ep_connecting(epoll_fd, sfd, data) == -1){
        info->rep = REP_GENFAIL;
        return SUCCESS;
    }

    return SUCCESS;
}

int init_bind(){

}

int init_udpa(){

}

int recv_eof(struct epoll_data_s *data){
    if(!data || !data->shared){
        return SUCCESS;
    }
    if(data->self_fd == data->shared->clientfd){
        data->shared->cfd_state = STATE_RDCLOSE;
        data->shared->sfd_state = STATE_WRCLOSE;
        if(data->shared->s_outbuff.used == 0){
            shutdown(data->shared->serverfd, SHUT_WR);
        }
    }
    else if(data->self_fd == data->shared->serverfd){
        data->shared->sfd_state = STATE_RDCLOSE;
        data->shared->cfd_state = STATE_WRCLOSE;
        if(data->shared->c_outbuff.used == 0){
            shutdown(data->shared->clientfd, SHUT_WR);
        }
    }

    return SUCCESS;
}

int read_data(int self_fd, struct epoll_data_s *data){
    struct ringbuff_s *outbuff = NULL;
    if(self_fd == data->shared->clientfd){
        outbuff = &data->shared->s_outbuff;
    }
    else if(self_fd == data->shared->serverfd){
        outbuff = &data->shared->c_outbuff;
    }

    size_t freebuff = outbuff->capacity - outbuff->used;
    if(freebuff == 0){ // Buffer is full
        return SUCCESS;
    }
    uint8_t buffer[freebuff];
    memset(&buffer, 0, freebuff);
    int recv_ret = recv(self_fd, &buffer, freebuff, 0);
    if(recv_ret == 0){
        return RECV_EOF;
    }
    else if(recv_ret == -1){
        return RECV_ERR;
    }
    if(write_ringbuff(outbuff, buffer, recv_ret) != recv_ret){
        return BUFFWR_ERR;
    }

    return SUCCESS;
}

int send_data(int self_fd, struct epoll_data_s *data){
    struct ringbuff_s *outbuff = NULL;
    if(self_fd == data->shared->clientfd){
        outbuff = &data->shared->c_outbuff;
    }
    else if(self_fd == data->shared->serverfd){
        outbuff = &data->shared->s_outbuff;
    }

    uint8_t buffer[outbuff->used];
    memset(&buffer, 0, outbuff->used);
    size_t read = peek_ringbuff(outbuff, buffer, outbuff->used);
    int sent = send(self_fd, &buffer, read, 0);
    if(sent == -1){
        return -1; // I guess bro
    }
    consume_ringbuff(outbuff, sent);
    if(read != sent){
        return sent;
    }

    return SUCCESS;
}
