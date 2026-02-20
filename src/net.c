#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>

#include "net.h"
#include "config.h"
#include "epoll_data.h"

void fd_nonblocking(int fd){
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

int init_listeners(int epoll_fd, struct configs_s *configs){
    struct listen_addrs_s *current = configs->addrs; //2d linked list??
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