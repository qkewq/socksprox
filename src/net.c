#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

#include "net.h"
#include "config.h"
#include "epoll_data.h"
#include "socks.h"
#include "logger.h"

void fd_nonblocking(int fd){
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

int v6flag(int fd){
	int flag = 1;
	if(setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &flag, sizeof(flag)) != 0){
		return 0;
	}

	return 1;
}

int init_listeners(int epoll_fd, Sockaddr_ll *current){
	if(!current){
		return -1;
	}
	while(current){
		int fd = socket(current->sa->sa_family, SOCK_STREAM, 0);
		if(fd == -1){
			return 1;
		}
		if(current->sa->sa_family == AF_INET6){
			if(!v6flag(fd)){
				return -1;
			}
		}

		if(bind(fd, current->sa, current->addrlen) == -1){
			return -1;
		}

		log_error(L_BINDLISTENER);

		fd_nonblocking(fd);
		listen(fd, SOMAXCONN);

		if(ep_add_listener(epoll_fd, fd) != 0){
			return -1;
		}

		current = current->next;
	}

	return 0;
}

int listen_err(int epoll_fd, uint32_t ev, Data *data){
	ep_delete_fd(epoll_fd, data->self_fd);

	return 0;
}

int accept_new_client(int epoll_fd, int fd){
	int new_fd = accept(fd, NULL, NULL);
	if(new_fd == -1){
		return -1;
	}

	fd_nonblocking(new_fd);
	Data *data = malloc(sizeof(Data));
	if(data == NULL){
		close(new_fd);
		log_error(L_MALLOCERROR);
		return -1;
	}

	Shared *shared = malloc(sizeof(Shared));
	if(shared == NULL){
		close(new_fd);
		free(data);
		log_error(L_MALLOCERROR);
		return -1;
	}

	Info *info = malloc(sizeof(Info));
	if(!info){
		close(new_fd);
		free(data);
		free(shared);
		log_error(L_MALLOCERROR);
		return -1;
	}

	memset(data, 0, sizeof(Data));
	memset(shared, 0, sizeof(Shared));
	memset(info, 0, sizeof(Info));
	data->type = TYPE_PEER;
	data->self_fd = new_fd;
	data->shared = shared;
	shared->state = STATE_WAITING_METHODS;
	shared->client_data = data;
	shared->server_data = NULL;
	shared->client_fd = new_fd;
	shared->server_fd = -1;
	shared->client_out.buff = malloc(OUTBUFFSIZE);
	shared->client_out.capacity = OUTBUFFSIZE;
	shared->server_out.buff = malloc(OUTBUFFSIZE);
	shared->server_out.capacity = OUTBUFFSIZE;
	shared->client_flags = 0;
	shared->server_flags = 0;
	shared->ptr = NULL;
	shared->info = info;
	info->method = 0xFF;
	info->rep = 0x0FF;
	info->cmd = 0xFF;
	info->atyp = 0xFF;
	info->resolver_ret = -1;
	info->ai_index = 0;
	info->ai = NULL;
	info->sa = NULL;

	if(shared->client_out.buff == NULL ||
		shared->server_out.buff == NULL){
		close(new_fd);
		free(shared->client_out.buff);
		free(shared->server_out.buff);
		free(shared);
		free(data);
		free(info);
		log_error(L_MALLOCERROR);
		return -1;
	}

	int ret = ep_add_new_client(epoll_fd, new_fd, data);
	if(ret != 0){
		close(new_fd);
		free(data);
		free(shared->client_out.buff);
		free(shared->server_out.buff);
		free(shared);
		free(info);
		return -1;
	}

	log_access(A_ACCEPTNEWCLIENT, new_fd, NULLFD);
	return 0;
}

int init_connect(int epoll_fd, Shared *shared){
	Info *info = shared->info;
	if(!info->ai && !info->sa){
		info->rep = REP_GENFAIL;
		return 0;
	}

	int sfd = -1;
	if(info->sa){ // Got ipv4 or ipv6
		if(info->sa->sa_family == AF_INET){ // ipv4
			sfd = socket(AF_INET, SOCK_STREAM, 0);
			if(sfd == -1){
				info->rep = REP_GENFAIL;
				return 0;
			}
			fd_nonblocking(sfd);
			if(connect(sfd, info->sa, sizeof(struct sockaddr_in)) == -1){
				if(errno != EAGAIN && errno != EINPROGRESS){
					info->rep = REP_GENFAIL;
					return 0;
				}
			}
		}
		else if(info->sa->sa_family == AF_INET6){ // ipv6
			sfd = socket(AF_INET6, SOCK_STREAM, 0);
			if(sfd == -1){
				info->rep = REP_GENFAIL;
				return 0;
			}
			fd_nonblocking(sfd);
			if(connect(sfd, info->sa, sizeof(struct sockaddr_in6)) == -1){
				if(errno != EAGAIN && errno != EINPROGRESS){
					info->rep = REP_GENFAIL;
					return 0;
				}
			}
		}
	}
	else if(info->ai){ // Got domain name
		sfd = socket(info->ai->ai_family, info->ai->ai_socktype, info->ai->ai_protocol);
		if(sfd == -1){
			info->rep = REP_GENFAIL;
			return 0;
		}
		fd_nonblocking(sfd);
		if(connect(sfd, info->ai->ai_addr, info->ai->ai_addrlen) == -1){
			if(errno != EAGAIN && errno != EINPROGRESS){
				info->rep = REP_GENFAIL;
				return 0;
			}
		}
		info->ai_index = 0;
	}

	Data *data = malloc(sizeof(Data));
	if(!data){
		log_error(L_MALLOCERROR);
		info->rep = REP_GENFAIL;
		return 0;
	}

	data->type = TYPE_PEER;
	data->self_fd = sfd;
	data->shared = shared;
	shared->server_data = data;
	shared->server_fd = sfd;

	consume_ringbuff(&shared->server_out, OUTBUFFSIZE); // Clear temp inbuff
	if(ep_connecting(epoll_fd, sfd, data) == -1){
		info->rep = REP_GENFAIL;
		return 0;
	}

	shared->state = STATE_CONNECTING;
	return 0;
}

int init_bind(int epoll_fd, Shared *shared, Configs *configs){
	Info *info = shared->info;
	if(!info->sa && !info->ai){
		info->rep = REP_GENFAIL;
		return 0;
	}

	int af = -1;
	if(info->sa){
		af = info->sa->sa_family;
	}
	else if(info->ai){
		af = info->ai->ai_family;
	}
	int sfd = socket(af, SOCK_STREAM, 0);
	if(sfd == -1){
		info->rep = REP_GENFAIL;
		return 0;
	}
	fd_nonblocking(sfd);

	struct Sockaddr_ll *listen_addr = configs->bind_listen;
	while(listen_addr){
		if(listen_addr->sa->sa_family == af){
			break;
		}
		listen_addr = listen_addr->next;
	}
	if(!listen_addr){
		info->rep = REP_BADATYP;
		close(sfd);
		return 0;
	}

	struct Sockaddr_ll *advertise_addr = configs->bind_advertise;
	while(configs->bind_advertise){
		if(advertise_addr->sa->sa_family == af){
			break;
		}
		advertise_addr = advertise_addr->next;
	}
	if(!advertise_addr){
		info->rep = REP_BADATYP;
		close(sfd);
		return 0;
	}

	if(bind(sfd, listen_addr->sa, listen_addr->addrlen) == -1){
		info->rep = REP_GENFAIL;
		close(sfd);
		return 0;
	}

	if(listen(sfd, SOMAXCONN) == -1){
		info->rep = REP_GENFAIL;
		close(sfd);
		return 0;
	}

	Data *data = malloc(sizeof(Data));
	if(!data){
		log_error(L_MALLOCERROR);
		info->rep = REP_GENFAIL;
		close(sfd);
		return 0;
	}

	data->type = TYPE_PEER;
	data->self_fd = sfd;
	data->shared = shared;
	shared->server_data = data;
	shared->server_fd = sfd;

	info->rep = REP_SUCCESS;
	uint8_t rep_info[3] = {0};
	rep_info[0] = SOCKS5_VERSION;
	rep_info[1] = info->rep;
	rep_info[2] = SOCKS5_RESV;
	write_ringbuff(&shared->client_out, rep_info, sizeof(rep_info));

	uint8_t atype = 0;
	if(af == AF_INET){
		struct sockaddr_in *sa = (struct sockaddr_in *)advertise_addr;
		atype = ATYP_IPV4;
		write_ringbuff(&shared->client_out, &atype, 1);
		write_ringbuff(&shared->client_out, (uint8_t *)&sa->sin_addr, 4);
		write_ringbuff(&shared->client_out, (uint8_t *)&sa->sin_port, 2);
		if(ep_waiting_send(epoll_fd, shared->client_fd, shared->client_data) == -1){
			info->rep = REP_GENFAIL;
			return 0;
		}
	}
	else if(af == AF_INET6){
		struct sockaddr_in6 *sa = (struct sockaddr_in6 *)advertise_addr;
		atype = ATYP_IPV6;
		write_ringbuff(&shared->client_out, &atype, 1);
		write_ringbuff(&shared->client_out, (uint8_t *)&sa->sin6_addr, 16);
		write_ringbuff(&shared->client_out, (uint8_t *)&sa->sin6_port, 2);
		if(ep_waiting_send(epoll_fd, shared->client_fd, shared->client_data) == -1){
			info->rep = REP_GENFAIL;
			return 0;
		}
	}

	consume_ringbuff(&shared->server_out, OUTBUFFSIZE); // Clear temp inbuff
	if(ep_add_new_client(epoll_fd, sfd, data) == -1){
		info->rep = REP_GENFAIL;
	}
	shared->state = STATE_SENDING_REPLY;
	return 0;
}

int init_udpa(){

}

int send_traffic(int fd, Ringbuff *outbuff){ // Sets flags on halfclose

}

int read_traffic(int fd, Ringbuff *outbuff){ // Sets flags on halfclose

}
