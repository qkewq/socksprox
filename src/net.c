#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
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
        return -1;
    }
    while(current != NULL){
        ai_current = current->addr;
        while(ai_current != NULL){
            int sfd = socket(ai_current->ai_family, ai_current->ai_socktype, ai_current->ai_protocol);
            if(sfd == -1){
                return -1;
            }
            if(bind(sfd, ai_current->ai_addr, ai_current->ai_addrlen) == -1){
                return -1;
            }
            fd_nonblocking(sfd);
            listen(sfd, SOMAXCONN);
            if(ep_add_listener(epoll_fd, sfd) != 0){
                return -1
            }
            ai_current = ai_current->ai_next;
        }
        current = current->next;
    }

    return 0;
}

int accept_new_client(int epoll_fd, int fd){
    int new_fd = accept(fd, NULL, NULL);
    if(new_fd == -1){
        return -1;
    }
    fd_nonblocking(new_fd);
    struct epoll_data_s *data = malloc(sizeof(struct epoll_data_s));
    if(data == NULL){
        close(new_fd);
        return -1
    }
    struct shared_data_s *shared = malloc(sizeof(struct shared_data_s));
    if(shared == NULL){
        close(new_fd);
        free(data);
        return -1;
    }
    memset(data, 0, sizeof(*data));
    memset(shared, 0, sizeof(*shared));
    data->is_listener = TYPE_NOLISTENER;
    data->self_fd = new_fd;
    data->shared = shared;
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
        return -1;
    }

    if(ep_add_new_client(epoll_fd, new_fd, data) == -1){
        close(new_fd);
        free(data);
        return -1;
    }

    return 0;
}

int init_connect(int epoll_fd, struct shared_data_s *shared){
    if(!shared || !shared->ptr || (!shared->ptr->ai && !shared->ptr->sa)){
        return -1;
    }
    if(shared->rep != 0xFF){
        return -1;
    }
    if(shared->sa){
        if(sa->sa_family == AF_INET){
            (struct sockaddr_in *)sa;
            sfd = socket(AF_INET, SOCK_STREAM, 0);
            if(sfd == -1){
                shared->ptr->rep = REP_GENFAIL;
                return 0;
            }
            fd_nonblocking(sfd);
            if(connect(sfd, sa, sizeof(*sa)) == -1){
                if(errno != EAGAIN || errno != EINPROGESS){
                    shared->ptr->rep = REP_GENFAIL;
                    return 0;
                }
            }
        }
        else if(sa->sa_family == AF_INET6){
            (struct sockaddr_in6 *)sa;
            sfd = socket(AF_INET6, SOCK_STREAM, 0);
            if(sfd == -1){
                shared->ptr->rep == REP_GENFAIL;
                return 0;
            }
            fd_nonblocking(sfd);
            if(connect(sfd, sa, sizeof(*sa)) == -1){
                if(errno != EAGAIN || errno != EINPROGRESS){
                    shared->ptr->rep = REP_GENFAIL
                    return 0;
                }
            }
        }
    }
    else if(shared->ai){
        sfd = socket(shared->ptr->ai->ai_family, shared->ptr->ai->ai_socktype, shared->ptr->ai->ai_protocol);
        if(sfd == -1){
            shared->ptr->rep = REP_GENFAIL;
            return 0;
        }
        fd_nonblocking(sfd);
        if(connect(sfd, shared->ai->ai_addr, shared->ai->ai_addrlen) == -1){
            if(errno != EAGAIN || errno != EINPROGRESS){
                shared->ptr->rep = REP_GENFAIL;
                return 0;
            }
        }
        shared->ptr->ai_index = 1;
    }
    struct epoll_data_s *data = malloc(sizeof(struct epoll_data_s));
    if(!data){
        shared->ptr->rep = REP_GENFAIL;
        return 0;
    }
    data->is_listener = TYPE_NOLISTENER;
    data->self_fd = sfd;
    data->shared = shared;
    shared->serverfd = sfd;
    shared->sfd_state = STATE_CONNECTING;
    memset(shared->s_outbuff, 0, OUTBUFFSIZE); // Clear temp inbuff
    if(ep_connecting(epoll_fd, sfd, data) == -1){
        shared->ptr->rep = REP_GENFAIL;
        return 0;
    }

    return 0;
}

int init_bind(){

}

int init_udpa(){

}
