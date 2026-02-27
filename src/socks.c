#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <errno.h>

#include "socks.h"
#include "net.h"
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
    uint8_t buffer[freebuff];
    memset(&buffer, 0, freebuff);
    int recv_ret = recv(data->self_fd, buffer, freebuff, 0);
    if(recv_ret == 0){
        //recv_eof();
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
    peek_ringbuff(&data->shared->s_outbuff, &check2[0], sizeof(check2));
    if(data->shared->s_outbuff.used < check2[1] + 2){ // Check if we have the entire message
        return 0;
    }
    consume_ringbuff(&data->shared->s_outbuff, sizeof(check2));
    if(vercheck(check2[0])){
        return -1;
    }
    uint8_t methods[255] = {0};
    uint8_t selected = 0xFF;
    size_t len_methods = peek_ringbuff(&data->shared->s_outbuff, &methods[0], check2[1]);
    for(int i = 0; i < check2[1]; i++){
        if(configs->methods[methods[i]] == 1){
            selected = methods[i];
            break;
        }
    }
    consume_ringbuff(&data->shared->s_outbuff, len_methods);
    uint8_t reply[2] = {SOCKS5_VERSION, selected};
    write_ringbuff(&data->shared->c_outbuff, &reply[0], sizeof(reply));
    data->shared->cfd_state = STATE_SENDINGMETHOD;
    struct method_s *method = malloc(sizeof(struct method_s));
    data->shared->ptr = method;
    if(!data->shared->ptr){
        return -1;
    }
    method->method = selected;
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
    struct epoll_data_s *data = event->data.ptr;
    uint8_t dst[2] = {0};
    size_t peek = peek_ringbuff(&data->shared->c_outbuff, &dst[0], sizeof(dst));
    int sent = send(data->self_fd, &dst, peek, 0);
    if(sent == -1){
        return -1;
    }
    if(sent < peek){
        consume_ringbuff(&data->shared->c_outbuff, sent);
        return 0;
    }
    consume_ringbuff(&data->shared->c_outbuff, peek);
    if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
        return -1;
    }
    struct method_s *method = data->shared->ptr;
    switch(method->method){
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

int waitingcommand(int epoll_fd, struct epoll_data_s *data, struct configs_s *configs){
    size_t freebuff = data->shared->s_outbuff.capacity - data->shared->s_outbuff.used;
    if(data->shared->ptr){
        free(data->shared->ptr);
        data->shared->ptr = NULL;
    }
    uint8_t buffer[freebuff];
    memset(&buffer, 0, freebuff);
    int recv_ret = recv(data->self_fd, buffer, freebuff, 0);
    if(recv_ret == 0){
        //recv_eof();
        return 0;
    }
    else if(recv_ret == -1){
        return -1;
    }
    if(write_ringbuff(&data->shared->s_outbuff, buffer, recv_ret) != recv_ret){
        return -1;
    }
    uint8_t req[300] = {0};
    size_t peek = peek_ringbuff(&data->shared->s_outbuff, &req[0], sizeof(req));
    if(peek == 0){
        return -1;
    }
    if(peek < 5){ // Need at least 5 to determine length of req
        return 0;
    }
    size_t total_len = 6; // 6 fixed bytes
    uint8_t addrtype = req[3];
    uint8_t command = req[1];
    switch(addrtype){ // Determine total length
        case ATYP_IPV4:
            total_len += 4;
            break;
        case ATYP_DOMN:
            total_len += req[4];
            break;
        case ATYP_IPV6:
            total_len += 16;
            break;
        default:
            return -1;
            break;
    }
    if(peek < total_len){ // Do we have total length yet?
        return 0;
    }
    struct req_info_s *info = malloc(sizeof(struct req_info_s));
    if(!info){
        return -1;
    }
    info->ai_index = 0;
    info->ai = NULL;
    info->sa = NULL;
    info->rep = 0xFF;
    info->cmd = command;
    data->shared->ptr = info;
    if(vercheck(req[0])){
        return -1;
    }
    if(addrtype == ATYP_DOMN && configs->allow_domains == 0){
        info->rep = REP_BADATYP;
    }
    switch(addrtype){
        case ATYP_IPV4:{
            struct sockaddr_in *sa = malloc(sizeof(struct sockaddr_in));
            if(!sa){
                return -1;
            }
            sa->sin_family = AF_INET;
            memcpy(&sa->sin_port, &req[8], 2);
            memcpy(&sa->sin_addr, &req[4], 4);
            info->sa = (struct sockaddr *)sa;
            break;
        }
        case ATYP_DOMN:{
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            size_t namelen = req[4];
            char name[255] = {0};
            uint16_t nport = req[6 + namelen] | (req[5 + namelen] << 8);
            memcpy(&name, &req[5], namelen);
            char port[6] = {0};
            snprintf(port, sizeof(port), "%d", htons(nport));
            if(getaddrinfo(name, port, &hints, &info->ai) != 0){
                return -1;
            }
            break;
        }
        case ATYP_IPV6:{
            struct sockaddr_in6 *sa = malloc(sizeof(struct sockaddr_in6));
            if(!sa){
                return -1;
            }
            memset(sa, 0, sizeof(struct sockaddr_in6));
            sa->sin6_family = AF_INET6;
            memcpy(&sa->sin6_port, &req[20], 2);
            memcpy(&sa->sin6_addr, &req[4], 16);
            info->sa = (struct sockaddr *)sa;
            break;
        }
    }
    switch(command){
        case CMD_CONN:
            if(!configs->allow_connect){
                info->rep = REP_BADCOMM;
                break;
            }
            if(init_connect(epoll_fd, data->shared) == -1){ // on success advances state to connecting
                return -1;
            }
                break;
        case CMD_BIND:
            if(!configs->allow_bind){
                info->rep = REP_BADCOMM;
                break;
            }
            init_bind(); // on success advances state to sending reply
            break;
        case CMD_UDPA:
            if(!configs->allow_udpassoc){
                info->rep = REP_BADCOMM;
                break;
            }
            init_udpa(); // on success advances state to sending reply
            break;
        default:
            return -1;
            break;
    }
    if(info->rep != 0xFF){ // Sending error code
        consume_ringbuff(&data->shared->s_outbuff, peek);
        uint8_t rep[10] = {0};
        rep[0] = SOCKS5_VERSION;
        rep[1] = info->rep;
        rep[2] = SOCKS5_RESV;
        rep[3] = ATYP_IPV4;
        if(write_ringbuff(&data->shared->c_outbuff, &rep[0], sizeof(rep)) != sizeof(rep)){
            return -1;
        }
        data->shared->cfd_state = STATE_SENDINGREPLY;
        if(ep_waiting_send(epoll_fd, data->self_fd, data) == -1){
            return -1;
        }
        return 0;
    }
    data->shared->cfd_state = STATE_HALF;

    return 0;
}

int sendingreply(int epoll_fd, struct epoll_event *event){
    if(!event || event->events & EPOLLIN || !(event->events & EPOLLOUT) || !event->data.ptr){
        return -1;
    }
    struct epoll_data_s *data = event->data.ptr;
    if(!data->shared){
        return -1;
    }
    uint8_t reply[300] = {0};
    size_t peek = peek_ringbuff(&data->shared->c_outbuff, &reply[0], sizeof(reply));
    int sent = send(data->self_fd, &reply, peek, 0);
    if(sent == -1){
        return -1;
    }
    if(sent < peek){
        consume_ringbuff(&data->shared->c_outbuff, sent);
        return 0;
    }
    consume_ringbuff(&data->shared->c_outbuff, peek);
    if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
        return -1;
    }
    if(!data->shared->ptr){
        return -1;
    }
    struct req_info_s *info = data->shared->ptr;
    if(info->rep != REP_SUCCESS){
        return -1;
    }
    switch(info->cmd){
        case CMD_CONN:
            data->shared->cfd_state = STATE_HALF;
            break;
        case CMD_BIND: //!!
            return -1;
            break;
        case CMD_UDPA: //!!
            return -1;
            break;
    }

    return 0;
}

int connecting(int epoll_fd, struct epoll_event *event){
    if(!event || !event->data.ptr){
        return -1;
    }
    struct epoll_data_s *data = event->data.ptr;
    if(!(event->events & EPOLLOUT || event->events & EPOLLERR)){
        return -1;
    }
    int error = 0;
    int errlen = sizeof(error);
    if(getsockopt(data->self_fd, SOL_SOCKET, SO_ERROR, &error, &errlen) == -1){
        return -1;
    }
    struct sockaddr_storage ss;
    struct req_info_s *info = data->shared->ptr;
    socklen_t sslen = sizeof(ss);
    memset(&ss, 0, sizeof(ss));
    if(error == 0){
        info->rep = REP_SUCCESS;
        if(getsockname(data->self_fd, (struct sockaddr *)&ss, &sslen) == -1){
            return -1;
        }
        uint8_t rep_info[3] = {0};
        rep_info[0] = SOCKS5_VERSION;
        rep_info[1] = info->rep;
        rep_info[2] = SOCKS5_RESV;
        write_ringbuff(&data->shared->c_outbuff, &rep_info[0], 3);
        int family = ss.ss_family;
        uint8_t atyp = 0;
        data->shared->cfd_state = STATE_SENDINGREPLY;
        if(family == AF_INET){
            struct sockaddr_in *sa = (struct sockaddr_in *)&ss;
            atyp = ATYP_IPV4;
            write_ringbuff(&data->shared->c_outbuff, &atyp, 1);
            write_ringbuff(&data->shared->c_outbuff, (uint8_t *)&sa->sin_addr, 4);
            write_ringbuff(&data->shared->c_outbuff, (uint8_t *)&sa->sin_port, 2);
            if(ep_waiting_send(epoll_fd, data->shared->clientfd, data->shared->c_data) == -1){
                return -1;
            }
            if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
                return -1;
            }
            data->shared->sfd_state = STATE_HALF;
            return 0;
        }
        else if(family == AF_INET6){
            struct sockaddr_in6 *sa = (struct sockaddr_in6 *)&ss;
            atyp = ATYP_IPV6;
            write_ringbuff(&data->shared->c_outbuff, &atyp, 1);
            write_ringbuff(&data->shared->c_outbuff, (uint8_t *)&sa->sin6_addr, 16);
            write_ringbuff(&data->shared->c_outbuff, (uint8_t *)&sa->sin6_port, 2);
            if(ep_waiting_send(epoll_fd, data->shared->clientfd, data->shared->c_data) == -1){
                return -1;
            }
            if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
                return -1;
            }
            data->shared->sfd_state = STATE_HALF;
            return 0;
        }
    }
    if(info->ai && info->ai->ai_next){
        info->ai_index += 1;
        struct addrinfo *current = info->ai;
        for(int i = 0; i < info->ai_index; i++){
            current = current->ai_next;
            if(!current){
                break;
            }
        }
        if(current){
            connect(data->self_fd, current->ai_addr, current->ai_addrlen);
            return 0;
        }
    }
    if(error & ENETUNREACH){
        info->rep = REP_NUNRECH;
    }
    else if(error & EHOSTUNREACH){
        info->rep = REP_HUNRECH;
    }
    else if(error & ETIMEDOUT){
        info->rep = REP_TTLEXPR;
    }
    else if(error & ECONNREFUSED){
        info->rep = REP_REFUSED;
    }
    else{
        info->rep = REP_GENFAIL;
    }
    uint8_t reply[10] = {0};
    reply[0] = SOCKS5_VERSION;
    reply[1] = info->rep;
    reply[2] = SOCKS5_RESV;
    reply[3] = ATYP_IPV4;
    write_ringbuff(&data->shared->c_outbuff, &reply[0], 10);
    if(ep_waiting_send(epoll_fd, data->shared->clientfd, data->shared->c_data) == -1){
        return -1;
    }
    if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
        return -1;
    }
    data->shared->cfd_state = STATE_SENDINGREPLY;

    return 0;
}

int half(int epoll_fd, struct epoll_data_s *data){
    if(!data || !data->shared){
        return -1;
    }
    if(data->shared->cfd_state != STATE_HALF || data->shared->sfd_state != STATE_HALF){
        return 0;
    }
    data->shared->cfd_state = STATE_FULL;
    data->shared->sfd_state = STATE_FULL;
    if(data->shared->ptr){
        struct req_info_s *info = data->shared->ptr;
        if(info->sa){
            free(info->sa);
        }
        else if(info->ai){
            freeaddrinfo(info->ai);
        }
        free(info);
        data->shared->ptr = NULL;
    }
    consume_ringbuff(&data->shared->c_outbuff, OUTBUFFSIZE);
    consume_ringbuff(&data->shared->s_outbuff, OUTBUFFSIZE);

    return 0;
}

int forward_traffic(int epoll_fd, uint32_t event, struct epoll_data_s *data){
    if(!data || !data->shared || !data->shared){
        return -1;
    }
    int oppfd = -1;
    struct epoll_data_s *oppdata = NULL;
    if(data->self_fd== data->shared->clientfd){
        oppfd = data->shared->serverfd;
        oppdata = data->shared->s_data;
    }
    else if(data->self_fd == data->shared->serverfd){
        oppfd = data->shared->clientfd;
        oppdata = data->shared->c_data;
    }

    if(event & EPOLLIN){
        if(read_data(data->self_fd, data) == -1){
            return -1;
        }
        if(ep_waiting_send(epoll_fd, oppfd, oppdata) == -1){
            return -1;
        }
    }
    else if(event & EPOLLOUT){
        int send_ret = send_data(data->self_fd, data);
        if(send_ret == -1){
            return -1;
        }
        else if(send_ret == 0){
            if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
                return -1;
            }
        }
    }

    return 0;
}

uint16_t socks5(int epoll_fd, struct epoll_event *event, struct configs_s *configs){
    if(!event || !event->data.ptr){
        return -1;
    }
    struct epoll_data_s *data = event->data.ptr;
    if(!data->shared || !configs){
        return -1; // CHANGE THESE TO ERROR CODES!!
    }
    uint32_t ev = event->events;

    if(data->is_listener == TYPE_ISLISTENER || data->self_fd == -1){
        return -1;
    }

    if(data->shared->sfd_state == STATE_STATELESS &&
        data->shared->cfd_state == STATE_STATELESS){
        return -1;
    }

    if(data->shared->sfd_state == STATE_HALF &&
        data->shared->cfd_state == STATE_HALF){
        half(epoll_fd, data);
        forward_traffic(epoll_fd, ev, data);
    }

    if(data->shared->cfd_state == STATE_FULL &&
        data->shared->sfd_state == STATE_FULL){
        forward_traffic(epoll_fd, ev, data);
    }
// Each state has a function responsible for that state
// Changing state is passing responsibility to another function
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
                if(!waitingcommand(epoll_fd, data, configs)){
                    return -1;
                }
                break;
            case STATE_SENDINGREPLY:
                if(!sendingreply(epoll_fd, event)){
                    return -1;
                }
                break;
            case STATE_SENDINGREPLY_2:
                break;
            case STATE_HALF: //!!
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
        switch(data->shared->sfd_state){
            case STATE_CONNECTING:
                if(!connecting(epoll_fd, event)){
                    return -1;
                }
                break;
            case STATE_BINDLISTENING:
                break;
            case STATE_HALF: //!!
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
