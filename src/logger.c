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
#include <arpa/inet.h>

#include "logger.h"
#include "config.h"

#define LOG_CUSTOM      0x00
#define LOG_ERROR       0x01
#define LOG_ACCESS      0x02
#define LOG_SIG_JOIN    0xFF

#define FILE_ACCESS     0x00
#define FILE_ERROR      0x01

Logger logger = {0}; // Global extern declaration

const char *error_strings[] = {
	"INFO|SPSTARTUP-Socksprox started and parsed configs",
	"INFO|LOGGERSETUP-Start and set up logging",
	"INFO|EPOLLCREATE-Created the epoll instance",
	"EMERG|EPOLLERROR-There was a fatal error in the epoll instance",
	"INFO|LISTENINIT-Listeners are bound and waiting for connections",
	"EMERG|LISTENERROR-Could not setup listeners",
	"INFO|RESOLVERSTART-Started the async DNS resolver",
	"WARN|RESOLVERERROR-Failed to start the async dns resolver",
	"INFO|NORESOLVER-Not starting resolver, not domain names allowed",
	"EMERG|MALLOCERROR-Memory allocation operation failed",
	"WARN|EPOLLERRORADD-Error adding fd to epoll",
	"WARN|EPOLLERRORMOD-Error modifying fd in epoll",
	"WARN|EPOLLERRORDEL-Error deleting fd from epoll",
	"INFO|BINDLISTENER-Bound listener from config file",
	"EMERG|EPOLLWAITERROR-There was an error waiting for epoll to return",
	"WARN|EPOLLDATANULL-Epoll returned a null pointer",
	"WARN|NEWCLIENTERROR-There was an error accepting a new client",
	"WARN|NULLCHECKFAIL-Found NULL where data was expected",
	"WARN|GETSOCKOPTFAIL-Error returned by getsockopt",
	"WARN|GETSOCKNAMEFAIL-Error returned by getsockname",
};

const char *access_formats[] = {
	"[%s]:%d-->socksprox|A new client connected to socksprox",
	"[%s]:%d--!socksprox|Connection closing during socks handshake",
	"[%s]:%d--!socksprox|Client sent invalid version during handshake",
	"[%s]:%d-->socksprox|Client requested command 'connect'",
	"[%s]:%d-->socksprox|Client requested command 'bind'",
	"[%s]:%d-->socksprox|Client requested command 'udp associate'",
	"[%s]:%d--!socksprox|Client requst blocked by firewall",
	"[%s]:%d-->socksprox|Method negotiated to 'no authentication'",
	"[%s]:%d-->socksprox|Method negotiated to 'username/password'",
	"[%s]:%d-->socksprox|Method negotiated to 'gssapi'",
	"[%s]:%d--!socksprox|No method could be negotiated",
	"[%s]:%d-->socksprox-->[%s]:%d|Successful proxy connection made",
	"[%s]:%d-->socksprox<--[%s]:%d|Peer connected to proxied listener",
	"[%s]:%d-->socksprox<->[%s]:%d|UDP relay port established",
};

int write_custom(char *str, FILE *file, struct tm *t){
	fprintf(file, "%d-%d-%d_%d:%d:%d-%s\n", t->tm_year + 1900, t->tm_mon + 1,
			t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, str);

	return 0;
}

int write_error(uint8_t code, FILE *file, struct tm *t){
	fprintf(file, "%d-%d-%d_%d:%d:%d-%s\n", t->tm_year + 1900, t->tm_mon + 1,
			t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, error_strings[code]);

	return 0;
}

int get_ip_port(char *ip_dst, int ip_dst_sz, uint16_t *port_dst, struct sockaddr_storage *src){
	if(src->ss_family == AF_INET){
		struct sockaddr_in *sin = (struct sockaddr_in *)src;
		inet_ntop(AF_INET, sin, ip_dst, ip_dst_sz);
		*port_dst = sin->sin_port;
	}
	else if(src->ss_family == AF_INET6){
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)src;
		inet_ntop(AF_INET6, sin6, ip_dst, ip_dst_sz);
		*port_dst = sin6->sin6_port;
	}

	return 0;
}

int write_access(Alog *access, FILE *file, struct tm *t){
	char client_ip[40] = {0};
	char server_ip[40] = {0};
	uint16_t client_port;
	uint16_t server_port;
	get_ip_port(client_ip, sizeof(client_ip), &client_port, access->client);
	if(access->server->ss_family != 0){
		get_ip_port(server_ip, sizeof(server_ip), &server_port, access->server);
	}

	fprintf(file, "%d-%d-%d_%d:%d:%d-", t->tm_year + 1900, t->tm_mon + 1,
			t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
	fprintf(file, access_formats[access->code], client_ip, client_port, server_ip, server_port);
	fprintf(file, "\n");

	return 0;
}

int open_files(FILE **file1, FILE **file2){
	*file1 = fopen(logger.a_log_path, "a");
	*file2 = fopen(logger.e_log_path, "a");
	if(!*file1 || !*file2){
		return 1;
	}

	if(setvbuf(*file1, NULL, _IOLBF, 0) != 0 ||
		setvbuf(*file2, NULL, _IOLBF, 0) != 0){
		return 1;
	}

	return 0;
}

void *logger_th(void *arg){
	FILE *log_files[2];
	if(open_files(&log_files[FILE_ACCESS], &log_files[FILE_ERROR]) != 0){
		close(logger.r_pipe);
		pthread_exit(NULL);
	}

	struct pollfd pfd;
	pfd.fd = logger.r_pipe;
	pfd.events = POLLIN;
	int ready = 0;

	while(1){
		ready = poll(&pfd, 1, -1);
		if(pfd.revents & POLLIN){

			while(1){
				uint8_t log_type = 0xFF;
				uint8_t code = 0xFF;
				int read_ret = read(pfd.fd, &log_type, sizeof(log_type));
				if(read_ret == -1){
					if(errno == EAGAIN || EWOULDBLOCK){
						break;
					}
				}

				time_t now = time(NULL);
				struct tm t;
				localtime_r(&now, &t);

				switch(log_type){
					case LOG_CUSTOM:{
						uint8_t file = 0;
						uint8_t strsz = 0;
						if(read(pfd.fd, &file, sizeof(file)) < sizeof(file)){
							break;
						}
						if(read(pfd.fd, &strsz, sizeof(strsz)) < sizeof(strsz)){
							break;
						}
						strsz += 1;
						char str[strsz];
						if(read(pfd.fd, str, strsz) < strsz){
							break;
						}
						str[strsz - 1] = '\0';
						write_custom(str, log_files[file], &t);
						break;
					}
					case LOG_ERROR:{
						if(read(pfd.fd, &code, sizeof(code)) < sizeof(code)){
							break;
						}
						write_error(code, log_files[FILE_ERROR], &t);
						break;
					}
					case LOG_ACCESS:{
						struct sockaddr_storage ss_access[2];
						memset(&ss_access[0], 0, sizeof(ss_access[0]));
						memset(&ss_access[1], 0, sizeof(ss_access[1]));
						Alog access;
						memset(&access, 0, sizeof(Alog));
						access.client = &ss_access[0];
						access.server = &ss_access[1];
						read(pfd.fd, &access.code, sizeof(access.code));
						read(pfd.fd, &ss_access[0], sizeof(ss_access[0]));
						read(pfd.fd, &ss_access[1], sizeof(ss_access[1]));
						write_access(&access, log_files[FILE_ACCESS], &t);
						break;
					}
					case LOG_SIG_JOIN:{
						pthread_exit(NULL);
						break;
					}
				}
			}
		}
	}
}

int init_logger(pthread_t *thread, Configs *configs){
	logger.a_log_path = configs->a_log;
	logger.e_log_path = configs->e_log;

	int pipefd[2];
	if(pipe2(pipefd, O_NONBLOCK) == -1){
		return -1;
	}

	logger.r_pipe = pipefd[0];
	logger.w_pipe = pipefd[1];

	return pthread_create(thread, NULL, logger_th, NULL);
}

void log_custom(char *str, uint8_t strsz, uint8_t file){
	uint8_t msg[255] = {0};
	msg[0] = LOG_CUSTOM;
	msg[1] = file;
	msg[2] = strsz;
	if(strsz > sizeof(msg) - 3){
		return;
	}
	memcpy(&msg[3], str, strsz);
	write(logger.w_pipe, &msg, strsz + 3);
}

void log_error(uint8_t code){
	uint8_t msg[2] = {0};
	msg[0] = LOG_ERROR;
	msg[1] = code;
	write(logger.w_pipe, &msg, sizeof(msg));
}

void log_access(Alog *access, uint8_t code){
	uint8_t msg[1 + sizeof(access->code) + (sizeof(struct sockaddr_storage) * 2)] = {0};
	msg[0] = LOG_ACCESS;
	msg[1] = code;
	memcpy(&msg[2], access->client, sizeof(*access->client));
	memcpy(&msg[2 + sizeof(*access->client)], access->server, sizeof(*access->server));
	write(logger.w_pipe, &msg, sizeof(msg));
}

void log_sig_join(pthread_t logger_thread){
	uint8_t msg = LOG_SIG_JOIN;
	write(logger.w_pipe, &msg, sizeof(msg));
	pthread_join(logger_thread, NULL);
}
