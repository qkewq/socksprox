#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>

#include "logger.h"
#include "config.h"

#define LOG_TYPE_ERR 0x00
#define LOG_TYPE_ACC 0x01
#define PTH_JOIN_SIG 0xff

// ERROR LOG FORMAT |  TYPE_ERR  |  LVL  |  CODE  |
//   FIXED 4 BYTES  |    0x00    |   1   |   2    |

// ACCESS LOG FORMAT|  TYPE_ACC  |  CODE  {|  ADDR TYPE  |    ADDR    |}x2
//      VARIABLE    |    0x01    |   2    {|      1      |  VARIABLE  |}x2
// ADDR TYPE is 0x00=Ignore, 0x01=IPv4, 0x03=Domain Name, 0x04=IPv6
// The first octet of domain name specifies the length of the address only
// Somtimes one or both addrs may be blank ¯\_(ツ)_/¯

// Added because fatal errors closed the thread before it could be logged
// THREAD JOIN SIG  |  SIGNAL  |
//   FIXED 1 BYTE   |   0xff   |
// Indicates to the logger that the parent process is closing
// The parent will hang on a join until the logger exits
// This should be treated as the last message the logger will receive on the pipe

const char *levels[] = {
    "EMERG",
    "ALERT",
    "CRITICAL",
    "ERROR",
    "WARN",
    "NOTICE",
    "INFO",
};

const char *conf_errors[] = {
    "FATAL CONFIG ERROR: Could not open config file",
    "FATAL CONFIG ERROR: Could not load addresses",
    "FATAL CONFIG ERROR: No IP addresses provided",
    "FATAL CONFIG ERROR: No port number provided",
    "FATAL CONFIG ERROR: No access log provided",
    "FATAL CONFIG ERROR: No error log provided",
    "FATAL CONFIG ERROR: No maximum connections provided",
    "FATAL CONFIG ERROR: No auth methods provided",
};

// "MNEMONIC-message"(keep it short)
const char *server_err[] = {
    "LOGGERSETUP-Start and set up logging",
    "EPOLLCREATE-Could not create epoll instance",
    "LISTENERROR-Could not setup listeners",
    "LISTENINITZ-Listeners are bound and waiting for connections",
};

const char *server_acc[] = {
    "",
};

int parse_err(uint8_t *buff, char *log, size_t log_size, struct tm *t){
    uint16_t code;
    memcpy(&code, &buff[2], sizeof(code));
    return snprintf(log, log_size, "%d-%d-%d_%d:%d:%d-%s|%s\n",
                    t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
                    t->tm_min, t->tm_sec, levels[buff[1]], server_err[code]);
}

int parse_acc(uint8_t *buff, char *log, size_t log_size, struct tm *t){
    uint16_t code;
    char c_addr[255];
    char p_addr[255];
    memcpy(&code, &buff[1], sizeof(code));
    int p_addr_index = 4;
    switch(buff[3]){
        case 0x01:
            p_addr_index += 4;
            memcpy(&c_addr, &buff[4], 4);
            break;
        case 0x03:
            p_addr_index += buff[4];
            memcpy(&c_addr, &buff[5], buff[4]);
            break;
        case 0x04:
            p_addr_index += 16;
            memcpy(&c_addr, &buff[5], 16);
            break;
        default:
            p_addr_index += 1;
            c_addr[0] = '\0';
    }
    switch(buff[p_addr_index]){
        case 0x01:
            memcpy(&p_addr, &buff[p_addr_index], 4);
            break;
        case 0x03:
            memcpy(&p_addr, &buff[p_addr_index + 1], buff[p_addr_index]);
            break;
        case 0x04:
            memcpy(&p_addr, &buff[p_addr_index], 16);
            break;
        default:
            p_addr[0] = '\0';
    }
    return snprintf(log, log_size, "%d-%d-%d_%d:%d:%d-Client{%s}|Peer{%s}-%s\n",
                    t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
                    t->tm_min, t->tm_sec, c_addr, p_addr, code);
}

void logger_join(pthread_t logger_thread, int log_fd){
    uint8_t sig = PTH_JOIN_SIG;
    write(log_fd, &sig, 1);
    pthread_join(logger_thread, NULL);
}

void *logger_th(void *arg){
    struct logs_s *logs = arg;
    struct pollfd pfd;
    pfd.fd = logs->r_pipe;
    pfd.events = POLLIN;
    int ready = 0;
    while(1){
        ready = poll(&pfd, 1, -1);
        if(pfd.revents & POLLIN){
            uint8_t buff[600] = {0};
            int r_len = read(pfd.fd, &buff, sizeof(buff));
            if(r_len <= 0){
                continue;
            }
            time_t now = time(NULL);
            struct tm t;
            localtime_r(&now, &t);
            char log[600] = {0};
            int log_len;
            switch(buff[0]){
                case LOG_TYPE_ERR:
                    log_len = parse_err(buff, log, sizeof(log), &t);
                    break;
                case LOG_TYPE_ACC:
                    log_len = parse_acc(buff, log, sizeof(log), &t);
                    break;
                case PTH_JOIN_SIG: // Parent thread is closing, there will be no more logs
                    pthread_exit(NULL);
            }
            switch(buff[0]){
                case LOG_TYPE_ERR:
                    fwrite(&log, log_len, 1, logs->e_log);
                    break;
                case LOG_TYPE_ACC:
                    fwrite(&log, log_len, 1, logs->a_log);
                    break;
            }
        }
    }
}

void error_log(int fd, uint8_t level, uint16_t code){
    uint8_t msg[4] = {0};
    msg[0] = LOG_TYPE_ERR;
    msg[1] = level;
    memcpy(&msg[2], &code, sizeof(code));
    write(fd, msg, sizeof(msg));
}

void access_log(int fd, uint16_t code, uint8_t client_atyp, char c_addr[255],
                uint8_t peer_atyp, char p_addr[255]){
    int msg_len = 5; // 5 fixed bytes + variable
    int c_addr_len = 0;
    int p_addr_len = 0;
    switch(client_atyp){
        case 0x01:
            msg_len += 4;
            c_addr_len = 4;
            break;
        case 0x03:
            msg_len += c_addr[0] + 1;
            c_addr_len = c_addr[0] + 1;
            break;
        case 0x04:
            msg_len += 16;
            c_addr_len = 16;
            break;
    }
    int p_index = 4;
    switch(peer_atyp){
        case 0x01:
            msg_len += 4;
            p_addr_len = 4;
            break;
        case 0x03:
            msg_len += p_addr[0] + 1;
            p_addr_len = p_addr[0] + 1;
            break;
        case 0x04:
            msg_len += 16;
            p_addr_len = 16;
            break;
    }

    uint8_t msg[msg_len];
    msg[0] = LOG_TYPE_ACC;
    memcpy(&msg[1], &code, sizeof(code));
    msg[3] = client_atyp;
    memcpy(&msg[4], &c_addr, c_addr_len);
    msg[p_index] = peer_atyp;
    memcpy(&msg[p_index + 1], &p_addr, p_addr_len);
    write(fd, &msg, sizeof(msg));
}

int open_logs(struct logs_s *logs, char *a_log, char *e_log){
    logs->a_log = fopen(a_log, "a");
    logs->e_log = fopen(e_log, "a");
    if(logs->a_log == NULL || logs->e_log == NULL){
        return -1;
    }
    if(setvbuf(logs->a_log, NULL, _IOLBF, 0) != 0 ||
        setvbuf(logs->e_log, NULL, _IOLBF, 0) != 0){
        return -1;
    }

    int pipefd[2];
    if(pipe2(pipefd, (O_DIRECT | O_NONBLOCK)) == -1){
        return -1;
    }
    logs->r_pipe = pipefd[0];
    return pipefd[1];
}

void conf_error_write(FILE *file, int err_index, struct tm *t){
    char log[64];
    int log_len = 0;
    log_len = snprintf(log, sizeof(log), "%d-%d-%d_%d:%d:%d-%s\n",
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
        t->tm_min, t->tm_sec, conf_errors[err_index]);
    fwrite(log, log_len, 1, file);
}

int conf_error(uint16_t conf_ret, struct configs_s *configs){
    if(conf_ret & E_CONFIGOPEN || conf_ret & E_CNFNOE_LOG){
        return 0;
    }

    FILE *e_file = fopen(configs->e_log, "a");
    if(e_file == NULL){
        return 0;
    }

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    if(conf_ret & E_ADDRMALLOC){
        conf_error_write(e_file, 1, &t);
    }
    if(conf_ret & E_CNFNOADDRS){
        conf_error_write(e_file, 2, &t);
    }
    if(conf_ret & E_CNFNOPORTN){
        conf_error_write(e_file, 3, &t);
    }
    if(conf_ret & E_CNFNOA_LOG){
        conf_error_write(e_file, 4, &t);
    }
    if(conf_ret & E_CNFNOMXCON){
        conf_error_write(e_file, 6, &t);
    }
    if(conf_ret & E_CNFNOMETHS){
        conf_error_write(e_file, 7, &t);
    }

    fclose(e_file);
    return 0;
}
