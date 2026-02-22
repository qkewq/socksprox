#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>

#include "socks.h"
#include "epoll_data.h"
#include "config.h"

#define SOCKS5_VERSION 0x05
#define SOCKS5_RESV 0x00

int vercheck(uint8_t ver){
    if(ver != SOCKS5_VERSION){
        return 1;
    }
    return 0;
}

int waitingmethods(int epoll_fd, struct epoll_data_s *data, struct configs_s *configs){
    size_t freebuff = data->shared->s_outbuff.capacity - data->shared->s_outbuff.used;
    uint8_t buffer[freebuff] = {0};
    int recv_ret = recv(data->self_fd, buffer, freebuff, 0);
    if(recv_ret == 0){
        recv_eof();
        return 0;
    }
    else if(recv_ret == -1){
        return -1;
    }
    
    if(write_ringbuff(&data->shared->s_outbuff, buffer, recv_ret) != recv_ret){
        return -1;
    }
    if(data->shared->s_outbuff.used < 2){ // Need first 2 bytes to get length
        return 0;
    }
    uint8_t check2[2] = {0};
    peek_ringbuff(&data->shared->s_outbuff, &check2, sizeof(check2));
    if(data->shared->s_outbuff.used < check2[1] + 2){ // Check if we have the entire message
        return 0;
    }
    consume_ringbuff(&data->shared->s_outbuff.buff, sizeof(check2));
    if(vercheck(check2[0])){
        return -1;
    }
    uint8_t methods[255] = {0};
    uint8_t selected = 0xFF;
    size_t len_methods = peek_ringbuff(&data->shared->s_outbuff, &methods, check2[1]);
    for(int i = 0; i < check2[1]; i++){
        if(configs->methods[methods[i]] == 1){
            selected = methods[i];
            break;
        }
    }
    consume_ringbuff(&data->shared->s_outbuff, len_methods);
    uint8_t reply[2] = {SOCKS5_VERSION, selected};
    write_ringbuff(&data->shared->c_outbuff, &reply, sizeof(reply));
    data->shared->cfd_state = STATE_SENDINGMETHOD;
    data->shared->ptr = malloc(sizeof(struct method_s));
    if(!data->shared->ptr){
        return -1;
    }
    data->shared->ptr->method = selected;
    if(ep_waiting_send(epoll_fd, data->self_fd, data) == -1){
        return -1;
    }

    return 0;
}

int sendingmethod(int epoll_fd, struct epoll_event *event){
    if(event->events & EPOLLIN ||
        !(event->events & EPOLLOUT)){ // Should not be receiving in this state
        return -1;
    }
    struct epoll_data_s *data = event->data->ptr;
    uint8_t dst[2] = {0};
    size_t peek = peek_ringbuff(data->shared->c_outbuff, &dst, sizeof(dst));
    int sent = send(data->self_fd, &dst, peek, 0);
    if(sent == -1){
        return -1;
    }
    if(sent < peek){
        consume_ringbuff(data->shared->c_outbuff, sent);
        return 0;
    }
    consume_ringbuff(data->shared->c_outbuff, peek);
    if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
        return -1;
    }
    switch(data->shared->ptr->method){
        case METH_NOAUTH:
            data->shared->cfd_state = STATE_WAITINGCOMMAND;
            break;
        case METH_NOMETH:
            return -1;
            break;
        default:
            return -1;
            break;
    }

    return 0;
}

int waitingcommand(int epoll_fd, struct epoll_data_s *data, struct config_s *configs){
    size_t freebuff = data->shared->s_outbuff.capacity - data->shared->s_outbuff.used;
    if(data->shared->ptr){
        free(data->shared->ptr);
        data->shared->ptr = NULL;
    }
    uint8_t buffer[freebuff] = {0};
    int recv_ret = recv(data->self_fd, buffer, freebuff, 0);
    if(recv_ret == 0){
        recv_eof();
        return 0;
    }
    else if(recv_ret == -1){
        return -1;
    }
    if(write_ringbuff(&data->shared->s_outbuff, buffer, recv_ret) != recv_ret){
        return -1;
    }
    uint8_t req[300] = {0};
    size_t peek = peek_ringbuff(&data->shared->s_outbuff, &req, sizeof(req));
    if(peek == 0){
        return -1;
    }
    if(peek < 5){ // Need at least 5 to determine length of req
        return 0;
    }
    size_t total_len = 6; // 6 fixed bytes
    uint8_t addrtype = 0;
    uint8_t command = 0;
    uint16_t port = 0;
    switch(req[4]){
        case 0x01:
            total_len += 4;
            addrtype = 0x01;
            break;
        case 0x03:
            total_len += req[5];
            addrtype = 0x03;
            break;
        case 0x04:
            total_len += 16;
            addrtype = 0x04;
            break;
        default:
            return -1;
            break;
    }
    if(peek < total_len){
        return 0;
    }
    if(vercheck(check2[0])){
        return -1;
    }
}

uint16_t socks5(int epoll_fd, struct epoll_event *event, struct configs_s *configs){
    if(!event || !event->data->ptr || !event->data->ptr->shared || !configs){
        return -1; // CHANGE THESE TO ERROR CODES!!
    }
    uint32_t ev = event->events;
    struct epoll_data_s *data = event->data->ptr;

    if(data->is_listener == TYPE_ISLISTENER || data->self_fd == -1){
        return -1;
    }

    if(data->shared->sfd_state == STATE_STATELESS &&
        data->shared->cfd_state == STATE_STATELESS){
        return -1;
    }

    if(data->shared->cfd_state == STATE_FULL &&
        data->shared->sfd_state == STATE_FULL){
        forward_traffic(); // MAKE THIS IN THIS FILE
    }
// still working on this part
    if(data->self_fd == data->shared->clientfd){
        switch(data->shared->cfd_state){
            case STATE_WAITINGMETHODS:
                if(!waitingmethods(epoll_fd, data, configs)){
                    return -1;
                }
                break;
            case STATE_SENDINGMETHOD:
                if(!sendingmethod(epoll_fd, event)){
                    return -1;
                }
                break;
            case STATE_AUTHENTICATING:
                return -1;
                break;
            case STATE_WAITINGCOMMAND:
                break;
            case STATE_CONNECTING:
                break;
            case STATE_BINDLISTENING:
                break;
            case STATE_SENDINGREPLY:
                break;
            case STATE_SENDINGREPLY_2:
                break;
            case STATE_HALF:
                break;
            case STATE_FULL:
                break;
            case STATE_HALFCLOSE:
                break;
            default:
                break;
        }
    }
    else if(data->self_fd == data->shared->serverfd){
        switch(data->shared->serverfd){
            case STATE_CONNECTING:
                break;
            case STATE_BINDLISTENING:
                break;
            case STATE_HALF:
                break;
            case STATE_FULL:
                break;
            case STATE_HALFCLOSE:
                break;
            default:
                break;
        }
    }
    else{
        return -1;
    }
}
