#ifndef LOGGER_H
#define LOGGER_H

#define L_EMERG 0
#define L_ALERT 1
#define L_CRITICAL 2
#define L_ERROR 3
#define L_WARN 4
#define L_NOTICE 5
#define L_INFO 6

#define E_LOGGERSETUP 0x00
#define E_EPOLLCREATE 0x01

struct configs_s; // From config.h
struct logs_s{
    FILE *a_log;
    FILE *e_log;
    int r_pipe;
};

void *logger_th(void *arg); // Designed for thread
void logger_join(pthread_t logger_thread, int log_fd); // Ensures pending logs are written before joining
void error_log(int fd, uint8_t level, uint16_t code);
void access_log(int fd, uint16_t code, uint8_t client_atyp, char c_addr[255],
                uint8_t peer_atyp, char p_addr[255]);
int open_logs(struct logs_s *logs, char *a_log, char *e_log);
int conf_error(uint16_t conf_ret, struct configs_s *configs);


#endif
