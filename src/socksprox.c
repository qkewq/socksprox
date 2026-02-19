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
    pthread_create(&logger_thread, NULL, logger_th(&logs), &logs);

    error_log(log_fd, L_INFO, E_LOGGERSETUP);

    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1){
        error_log(log_fd, L_EMERG, E_EPOLLCREATE);
        return 1;
    }

    return 0;
}
