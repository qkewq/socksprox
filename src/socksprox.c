#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <signal.h>

#include "config.h"
#include "logger.h"
#include "net.h"
#include "epoll_data.h"
#include "socks.h"
#include "resolver.h"

int main(int argc, char *argv[]){
	signal(SIGPIPE, SIG_IGN); // SIGPIPEs are handled locally
	Configs configs;
	memset(&configs, 0, sizeof(configs));
	if(parse_configs(&configs)){
		return 1;
	}

	pthread_t logger_thread;
	if(init_logger(&logger_thread, &configs) != 0){ // Start logger
		return 1;
	}

	log_error(L_SPSTARTUP);
	log_error(L_LOGGERSETUP);

	struct epoll_event *events = malloc(sizeof(struct epoll_event) * configs.max_conns);
	int epoll_fd = epoll_create1(0); // Start epoll
	if(!events){
		log_error(L_MALLOCERROR);
		log_sig_join(logger_thread);
	}
	if(epoll_fd == -1){
		log_error(L_EPOLLERROR);
		log_sig_join(logger_thread);
		return 1;
	}

	log_error(L_EPOLLCREATE);

	if(init_listeners(epoll_fd, configs.listen_head) != 0){ // Create listeners
		log_error(L_LISTENERROR);
		log_sig_join(logger_thread);
		return 1;
	}

	log_error(L_LISTENINIT);

	pthread_t resolver_thread;
	if(configs.allow_domains){ // Start resolver if configured
		if(init_resolver(&resolver_thread, epoll_fd) != 0){
			log_error(L_RESOLVERERROR);
			configs.allow_domains = 0;
			log_error(L_NORESOLVER);
		}
		else{
			log_error(L_RESOLVERSTART);
		}
	}
	else{
		log_error(L_NORESOLVER);
	}

	while(1){ // Event loop
		int nfds = epoll_wait(epoll_fd, events, configs.max_conns, -1);
		if(nfds == -1){
			log_error(L_EPOLLWAITERROR);
			log_sig_join(logger_thread);
			return 1;
		}

		for(int i = 0; i < nfds; i++){
			if(!events[i].data.ptr){
				log_error(L_EPOLLDATANULL);
				continue;
			}

			Data *data = events[i].data.ptr;
			switch(data->type){
				case TYPE_LISTENER:
					if(events[i].events & EPOLLIN){
						if(accept_new_client(epoll_fd, data->self_fd) == -1){
							log_error(L_NEWCLIENTFAIL);
						}
					}
					else{
						log_error(L_LISTENERROR);
						log_sig_join(logger_thread);
						return 1;
					}
					break;
				case TYPE_PEER:
					if(events[i].events & (EPOLLIN | EPOLLOUT)){
						socks5(epoll_fd, &events[i], &configs);
					}
					break;
				case TYPE_RESOLVER:
					resolve_ret(epoll_fd, &configs); // Do not look in here
					break;
			}
		}
	}

	log_sig_join(logger_thread); // Ensures pending logs are written
	return 0;
}
