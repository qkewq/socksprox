#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>

#include "config.h"
#include "logger.h"
#include "net.h"
#include "epoll_data.h"
#include "socks.h"

int main(int argc, char *argv[]){
    for(int i = 0; i < argc; i++){ //Run config tester and exit
        if(strcmp(argv[i], "-t") == 0){
            test_configs();
            return 0;
        }
    }

    struct configs_s configs;
    struct logs_s logs;
    memset(&configs, 0, sizeof(configs));
    uint16_t conf_ret = parse_configs(&configs);
    if(conf_ret != 0){ // Log and exit on fatal config errors
        conf_error(conf_ret, &configs);
        return 1;
    }

    int log_fd = open_logs(&logs, configs.a_log, configs.e_log);
    if(log_fd == -1){
        return 1;
    }

    pthread_t logger_thread;
    if(pthread_create(&logger_thread, NULL, logger_th, &logs) != 0){
        return 1;
    }

    error_log(log_fd, L_INFO, E_LOGGERSETUP);

    struct epoll_event *events = malloc(sizeof(struct epoll_event) * configs.max_conns);
    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1 || events == NULL){
        error_log(log_fd, L_EMERG, E_EPOLLCREATE);
        logger_join(logger_thread, log_fd);
        return 1;
    }

    if(init_listeners(epoll_fd, &configs) != 0){
        error_log(log_fd, L_EMERG, E_LISTENERROR);
        logger_join(logger_thread, log_fd);
        return 1;
    }
    free_config_addrs(&configs);

    error_log(log_fd, L_INFO, E_LISTENINITZ);

    while(1){
        int nfds = epoll_wait(epoll_fd, events, configs.max_conns, -1);
        if(nfds == -1){
            return 1;
        }

        for(int i = 0; i < nfds; i++){
            if(!events[i].data.ptr){
                continue;
            }
            struct epoll_data_s *data = events[i].data.ptr;
            switch(data->is_listener){
                case TYPE_ISLISTENER:
                    if(events[i].events & (EPOLLERR | EPOLLHUP)){
                        listener_error(events[i]->events, events[i]->data->ptr);
                        logger_join(logger_thread, log_fd);
                        return 1;
                    }
                    else if(events[i].events & EPOLLIN){
                        accept_new_client(epoll_fd, data->self_fd);
                    }
                    break;
                case TYPE_NOLISTENER:
                    if(events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)){
                        freeclose(epoll_fd, &events[i]);
                    }
                    else if(events[i].events & (EPOLLHUP | EPOLLRDHUP)){
                        //socks5hup(events[i]->events, events[i]->data->ptr); // Make this function
                    }
                    else if(events[i].events & (EPOLLIN | EPOLLOUT)){
                        socks5(epoll_fd, &events[i], &configs);
                    }
                    break;
            }
        }
    }

    logger_join(logger_thread, log_fd); // Ensures pending logs are written
    return 0;
}
