#ifndef LOGGER_H
#define LOGGER_H

#define NULLFD -1

#define L_SPSTARTUP         0x00
#define L_LOGGERSETUP       0x01
#define L_EPOLLCREATE       0x02
#define L_EPOLLERROR        0x03
#define L_LISTENINIT        0x04
#define L_LISTENERROR       0x05
#define L_RESOLVERSTART     0x06
#define L_RESOLVERERROR     0x07
#define L_NORESOLVER        0x08
#define L_MALLOCERROR       0x09
#define L_EPOLLERRORADD     0x0A
#define L_EPOLLERRORMOD     0x0B
#define L_EPOLLERRORDEL     0x0C
#define L_BINDLISTENER      0x0D
#define L_EPOLLWAITERROR    0x0E
#define L_EPOLLDATANULL     0x0F
#define L_NEWCLIENTFAIL     0x10
#define L_NULLCHECKFAIL     0x11
#define L_GETSOCKOPTFAIL    0x12
#define L_GETSOCKNAMEFAIL   0x13

#define A_ACCEPTNEWCLIENT   0x00
#define A_CLOSEHANDSHAKE    0x01
#define A_VERSIONFAIL       0x02
#define A_COMMCONN          0x03
#define A_COMMBIND          0x04
#define A_COMMUDPA          0x05
#define A_FIREWALLDROP      0x06
#define A_METHNOAUTH        0x07
#define A_METHUSERPASS      0x08
#define A_METHGSSAPI        0x09
#define A_METHNOMETH        0x0A
#define A_CONNECTED         0x0B
#define A_BINDCONNECTED     0x0C
#define A_ASSOCIATED        0x0D

typedef struct Configs Configs;

typedef struct Logger{
	char *a_log_path;
	char *e_log_path;
	int r_pipe;
	int w_pipe;
} Logger;

typedef struct Alog{
	uint8_t code;
	struct sockaddr_storage *client;
	struct sockaddr_storage *server;
} Alog;

extern Logger logger;

int init_logger(pthread_t *thread, Configs *configs);
void log_custom(char *str, uint8_t strsz, uint8_t file);
void log_error(uint8_t code);
void log_access(Alog *access, uint8_t code);
void log_sig_join(pthread_t logger_thread);

#endif
