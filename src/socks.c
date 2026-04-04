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
#include "logger.h"
#include "resolver.h"
#include "firewall.h"

int vercheck(uint8_t ver){
	if(ver != SOCKS5_VERSION){
		return 1;
	}
	return 0;
}

int handshake_err(int epoll_fd, Data *data){
	log_access(data->shared->access, A_CLOSEHANDSHAKE);
	data->shared->state = STATE_CLOSED;
	ep_delete_fd(epoll_fd, data->self_fd);
	close(data->self_fd);
	free_data(data);

	return 0;
}

int reply_with_err(int epoll_fd, Data *data){
	if(data->shared->info->rep < 0x01 || data->shared->info->rep > 0x08){
		data->shared->info->rep = REP_GENFAIL;
	}
	uint8_t rep[10] = {0};
	rep[0] = SOCKS5_VERSION;
	rep[1] = data->shared->info->rep;
	rep[2] = SOCKS5_RESV;
	rep[3] = ATYP_IPV4;
	if(write_ringbuff(&data->shared->client_out, &rep[0], sizeof(rep)) != sizeof(rep)){
		handshake_err(epoll_fd, data);
		return 0;
	}
	if(ep_waiting_send(epoll_fd, data->shared->client_fd, data->shared->client_data) == -1){
		handshake_err(epoll_fd, data);
		return 0;
	}

	data->shared->state = STATE_SENDING_REPLY;
	return 0;
}

int init_command(int epoll_fd, Data *data, Configs *configs){
	Info *info = data->shared->info;
	switch(info->cmd){
		case CMD_CONN:
			log_access(data->shared->access, A_COMMCONN);
			if(!configs->allow_connect){
				info->rep = REP_BADCOMM;
				reply_with_err(epoll_fd, data);
				return 0;
			}
			init_connect(epoll_fd, data->shared);
			break;
		case CMD_BIND:
			log_access(data->shared->access, A_COMMBIND);
			if(!configs->allow_bind){
				info->rep = REP_BADCOMM;
				reply_with_err(epoll_fd, data);
				return 0;
			}
			init_bind(epoll_fd, data->shared, configs);
			break;
		case CMD_UDPA:
			log_access(data->shared->access, A_COMMUDPA);
			if(!configs->allow_udpassoc){
				info->rep = REP_BADCOMM;
				reply_with_err(epoll_fd, data);
				return 0;
			}
			init_udpa(epoll_fd, data->shared, configs);
			break;
		default:
			info->rep = REP_BADCOMM;
			reply_with_err(epoll_fd, data);
			break;
	}

	return 0;
}

int waitingmethods(int epoll_fd, Data *data, Configs *configs, uint32_t ev){
	if(!(ev & EPOLLIN) || data->self_fd != data->shared->client_fd){
		return 0;
	}

	size_t freebuff = data->shared->server_out.capacity - data->shared->server_out.used;
	uint8_t buffer[freebuff];
	memset(&buffer, 0, freebuff);
	int recv_ret = recv(data->self_fd, buffer, freebuff, 0);
	if(recv_ret <= 0){
		handshake_err(epoll_fd, data);
		return 0;
	}

	if(write_ringbuff(&data->shared->server_out, buffer, recv_ret) != recv_ret){
		handshake_err(epoll_fd, data);
		return 0;
	}
	if(data->shared->server_out.used < 2){ // Need first 2 bytes to get total length
		return 0;
	}

	uint8_t first_two[2] = {0};
	uint8_t version = 0;
	uint8_t nmethods = 0;
	peek_ringbuff(&data->shared->server_out, &first_two[0], sizeof(first_two));
	version = first_two[0];
	nmethods = first_two[1];
	if(data->shared->server_out.used < 2 + nmethods){ // Check if we have the entire message
		return 0;
	}

	consume_ringbuff(&data->shared->server_out, sizeof(version) + sizeof(nmethods));

	if(vercheck(version)){
		log_access(data->shared->access, A_VERSIONFAIL);
		handshake_err(epoll_fd, data);
		return 0;
	}

	uint8_t methods[255] = {0};
	uint8_t selected = 0xFF;
	size_t len_methods = peek_ringbuff(&data->shared->server_out, &methods[0], nmethods);
	for(int i = 0; i < nmethods; i++){
		if(configs->methods[methods[i]] == 1){
			selected = methods[i];
			break;
		}
	}

	consume_ringbuff(&data->shared->server_out, len_methods);
	uint8_t reply[2] = {SOCKS5_VERSION, selected};
	write_ringbuff(&data->shared->client_out, &reply[0], sizeof(reply));
	data->shared->state = STATE_SENDING_METHOD;
	data->shared->info->method = selected;

	if(ep_waiting_send(epoll_fd, data->self_fd, data) == -1){
		handshake_err(epoll_fd, data);
		return 0;
	}

	return 0;
}

int sendingmethod(int epoll_fd, Data *data, uint32_t ev){
	if(!(ev & EPOLLOUT) || data->self_fd != data->shared->client_fd){
		return 0;
	}

	uint8_t reply[2] = {0};
	size_t peek = peek_ringbuff(&data->shared->client_out, &reply[0], sizeof(reply));
	int sent = send(data->self_fd, &reply, peek, 0);
	if(sent == -1){
		handshake_err(epoll_fd, data);
		return 0;
	}
	if(sent < peek){ // Check if all got sent
		consume_ringbuff(&data->shared->client_out, sent);
		return 0;
	}
	consume_ringbuff(&data->shared->client_out, peek);

	if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
		handshake_err(epoll_fd, data);
		return 0;
	}

	switch(data->shared->info->method){
		case METH_NOAUTH:
			log_access(data->shared->access, A_METHNOAUTH);
			data->shared->state = STATE_WAITING_COMMAND;
			break;
		case METH_USERPW:
			log_access(data->shared->access, A_METHUSERPASS);
			handshake_err(epoll_fd, data);
			break;
		case METH_GSSAPI:
			log_access(data->shared->access, A_METHGSSAPI);
			handshake_err(epoll_fd, data);
			break;
		case METH_NOMETH:
			log_access(data->shared->access, A_METHNOMETH);
			handshake_err(epoll_fd, data);
			break;
		default:
			handshake_err(epoll_fd, data);
			break;
	}

	return 0;
}

int waitingcommand(int epoll_fd, Data *data, Configs *configs, uint32_t ev){
	if(!(ev & EPOLLIN) || data->self_fd != data->shared->client_fd){
		return 0;
	}

	if(data->shared->info->resolver_ret != -1){ // Resolver returned
		if(data->shared->info->resolver_ret != 0){
			data->shared->info->rep = REP_GENFAIL;
			reply_with_err(epoll_fd, data);
		}
		init_command(epoll_fd, data, configs);
		if(data->shared->info->rep != 0xFF && data->shared->info->rep != REP_SUCCESS){
			reply_with_err(epoll_fd, data);
		}
		return 0;
	}

	size_t freebuff = data->shared->server_out.capacity - data->shared->server_out.used;
	uint8_t buffer[freebuff];
	memset(&buffer, 0, freebuff);
	int recv_ret = recv(data->self_fd, buffer, freebuff, 0);
	if(recv_ret <= 0){
		handshake_err(epoll_fd, data);
		return 0;
	}

	if(write_ringbuff(&data->shared->server_out, buffer, recv_ret) != recv_ret){
		handshake_err(epoll_fd, data);
		return 0;
	}

	uint8_t req[300] = {0};
	size_t peek = peek_ringbuff(&data->shared->server_out, &req[0], sizeof(req));
	if(peek < 5){ // Need at least 5 to determine length of req
		return 0;
	}

	size_t total_len = 6; // 6 fixed bytes
	uint8_t command = req[1];
	uint8_t addrtype = req[3];
	switch(addrtype){ // Determine total length
		case ATYP_IPV4:
			total_len += 4;
			break;
		case ATYP_DOMN:
			if(!configs->allow_domains){
				data->shared->info->rep = REP_BADATYP;
				reply_with_err(epoll_fd, data);
				return 0;
			}
			total_len += req[4];
			break;
		case ATYP_IPV6:
			total_len += 16;
			break;
		default:
			data->shared->info->rep = REP_BADATYP;
			reply_with_err(epoll_fd, data);
			return 0;
	}
	if(peek < total_len){ // Do we have total length yet?
		return 0;
	}

	Info *info = data->shared->info;
	info->cmd = command;
	info->atyp = addrtype;

	if(vercheck(req[0])){
		log_access(data->shared->access, A_VERSIONFAIL);
		handshake_err(epoll_fd, data);
		return 0;
	}

	switch(info->atyp){
		case ATYP_IPV4:{
			struct sockaddr_in *sa = malloc(sizeof(struct sockaddr_in));
			if(!sa){
				log_error(L_MALLOCERROR);
				return -1;
			}
			memset(sa, 0, sizeof(struct sockaddr_in));
			sa->sin_family = AF_INET;
			memcpy(&sa->sin_port, &req[8], 2);
			memcpy(&sa->sin_addr, &req[4], 4);
			info->sa = (struct sockaddr *)sa;
			break;
		}
		case ATYP_DOMN:{
			data->shared->state = STATE_ASYNC_DNS;
			resolve(data->shared, &req[4]);
			return 0;
		}
		case ATYP_IPV6:{
			struct sockaddr_in6 *sa = malloc(sizeof(struct sockaddr_in6));
			if(!sa){
				log_error(L_MALLOCERROR);
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


	if(firewall(info, configs)){
		log_access(data->shared->access, A_FIREWALLDROP);
		info->rep = REP_RULESET;
		reply_with_err(epoll_fd, data);
		return 0;
	}

	init_command(epoll_fd, data, configs);

	// If the init functions fail they will set info->rep and still return 0
	// If they succeed they will change state and return 0
	if(info->rep != 0xFF && info->rep != REP_SUCCESS){
		reply_with_err(epoll_fd, data);
	}

	return 0;
}

int sendingreply(int epoll_fd, Data *data, uint32_t ev){
	if(!(ev & EPOLLOUT) || data->self_fd != data->shared->client_fd){
		return 0;
	}

	uint8_t reply[300] = {0};
	size_t peek = peek_ringbuff(&data->shared->client_out, &reply[0], sizeof(reply));
	int sent = send(data->self_fd, &reply, peek, 0);
	if(sent == -1){
		handshake_err(epoll_fd, data);
		return 0;
	}
	if(sent < peek){ // Check if we sent it all
		consume_ringbuff(&data->shared->client_out, sent);
		return 0;
	}

	consume_ringbuff(&data->shared->client_out, peek);

	if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
		handshake_err(epoll_fd, data);
		return 0;
	}

	Info *info = data->shared->info;
	if(info->rep != REP_SUCCESS){
		handshake_err(epoll_fd, data);
		return 0;
	}

	switch(info->cmd){
		case CMD_CONN:
			log_access(data->shared->access, A_CONNECTED);
			data->shared->state = STATE_FULL;
			break;
		case CMD_BIND:
			if(data->shared->state == STATE_SENDING_REPLY){
				info->rep = 0xFF; // Reset for sending reply 2
				data->shared->state = STATE_BIND_LISTENING;
			}
			else if(data->shared->state == STATE_SENDING_REPLY_2){
				log_access(data->shared->access, A_BINDCONNECTED);
				data->shared->state = STATE_FULL;
			}
			break;
		case CMD_UDPA:
			data->shared->state = STATE_FULL_UDPA;
			break;
	}

	return 0;
}

int connecting(int epoll_fd, Data *data, uint32_t ev){
	if(!(ev & EPOLLOUT) || data->self_fd != data->shared->server_fd){
		return 0;
	}

	int error = 0;
	socklen_t errlen = sizeof(error);
	if(getsockopt(data->self_fd, SOL_SOCKET, SO_ERROR, &error, &errlen) == -1){
		log_error(L_GETSOCKOPTFAIL);
		data->shared->info->rep = REP_GENFAIL;
		reply_with_err(epoll_fd, data);
		return 0;
	}

	struct sockaddr_storage ss;
	struct Info *info = data->shared->info;
	socklen_t sslen = sizeof(ss);
	memset(&ss, 0, sizeof(ss));

	if(error == 0){ // Successful connection
		info->rep = REP_SUCCESS;
		if(getsockname(data->self_fd, (struct sockaddr *)&ss, &sslen) == -1){
			log_error(L_GETSOCKNAMEFAIL);
			info->rep = REP_GENFAIL;
			reply_with_err(epoll_fd, data);
			return 0;
		}

		memcpy(data->shared->access->server, &ss, sizeof(ss));

		uint8_t rep_info[3] = {0};
		rep_info[0] = SOCKS5_VERSION;
		rep_info[1] = info->rep;
		rep_info[2] = SOCKS5_RESV;
		write_ringbuff(&data->shared->client_out, &rep_info[0], sizeof(rep_info));
		int family = ss.ss_family;
		uint8_t atyp = 0;
		data->shared->state = STATE_SENDING_REPLY;

		if(family == AF_INET){
			struct sockaddr_in *sa = (struct sockaddr_in *)&ss;
			atyp = ATYP_IPV4;
			write_ringbuff(&data->shared->client_out, &atyp, 1);
			write_ringbuff(&data->shared->client_out, (uint8_t *)&sa->sin_addr, 4);
			write_ringbuff(&data->shared->client_out, (uint8_t *)&sa->sin_port, 2);
			if(ep_waiting_send(epoll_fd, data->shared->client_fd, data->shared->client_data) == -1){
				info->rep = REP_GENFAIL;
				reply_with_err(epoll_fd, data);
				return 0;
			}

			if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
				info->rep = REP_GENFAIL;
				reply_with_err(epoll_fd, data);
				return 0;
			}

			return 0;
		}
		else if(family == AF_INET6){
			struct sockaddr_in6 *sa = (struct sockaddr_in6 *)&ss;
			atyp = ATYP_IPV6;
			write_ringbuff(&data->shared->client_out, &atyp, 1);
			write_ringbuff(&data->shared->client_out, (uint8_t *)&sa->sin6_addr, 16);
			write_ringbuff(&data->shared->client_out, (uint8_t *)&sa->sin6_port, 2);
			if(ep_waiting_send(epoll_fd, data->shared->client_fd, data->shared->client_data) == -1){
				info->rep = REP_GENFAIL;
				reply_with_err(epoll_fd, data);
				return 0;
			}

			if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
				info->rep = REP_GENFAIL;
				reply_with_err(epoll_fd, data);
				return 0;
			}

			return 0;
		}
	}

	if(info->ai && info->ai->ai_next){ // Try next addr if it exists
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

	reply_with_err(epoll_fd, data);

	return 0;
}

int bindlistening(int epoll_fd, Data *data, uint32_t ev){
	if(!(ev & EPOLLIN) || data->self_fd != data->shared->server_fd){
		return 0;
	}

	Info *info = data->shared->info;
	struct sockaddr_storage ss;
	socklen_t ss_len = sizeof(ss);
	int new_fd = accept(data->self_fd, (struct sockaddr *)&ss, &ss_len);
	if(new_fd == -1){
		info->rep = REP_GENFAIL;
		reply_with_err(epoll_fd, data);
		return 0;
	}

	memcpy(data->shared->access->server, &ss, sizeof(ss));

	struct sockaddr *req_addr = NULL;
	if(info->sa){
		req_addr = info->sa;
	}
	else if(info->ai){
		req_addr = info->ai->ai_addr;
	}
	else{
		info->rep = REP_GENFAIL;
		reply_with_err(epoll_fd, data);
		return 0;
	}

	if(req_addr->sa_family != ss.ss_family){
		info->rep = REP_RULESET;
		reply_with_err(epoll_fd, data);
		return 0;
	}

	if(ss.ss_family == AF_INET){
		struct sockaddr_in *sa = (struct sockaddr_in *)&ss;
		struct sockaddr_in *req = (struct sockaddr_in *)req_addr;
		if(sa->sin_port != req->sin_port){
			info->rep = REP_RULESET;
			reply_with_err(epoll_fd, data);
			return 0;
		}
		if(sa->sin_addr.s_addr != req->sin_addr.s_addr){
			info->rep = REP_RULESET;
			reply_with_err(epoll_fd, data);
			return 0;
		}
	}
	else if(ss.ss_family == AF_INET6){
		struct sockaddr_in6 *sa = (struct sockaddr_in6 *)&ss;
		struct sockaddr_in6 *req = (struct sockaddr_in6 *)req_addr;
		if(sa->sin6_port != req->sin6_port){
			info->rep = REP_RULESET;
			reply_with_err(epoll_fd, data);
			return 0;
		}
		if(memcmp(&sa->sin6_addr, &req->sin6_addr, sizeof(struct in6_addr)) != 0){
			info->rep = REP_RULESET;
			reply_with_err(epoll_fd, data);
			return 0;
		}
	}

	close(data->self_fd);
	ep_delete_fd(epoll_fd, data->self_fd);
	data->self_fd = new_fd;
	data->shared->server_fd = new_fd;
	if(ep_add_new_client(epoll_fd, new_fd, data) == -1){
		info->rep = REP_GENFAIL;
		reply_with_err(epoll_fd, data);
		return 0;
	}

	info->rep = REP_SUCCESS;
	uint8_t rep_info[3] = {0};
	rep_info[0] = SOCKS5_VERSION;
	rep_info[1] = info->rep;
	rep_info[2] = SOCKS5_RESV;
	write_ringbuff(&data->shared->client_out, &rep_info[0], sizeof(rep_info));
	uint8_t atype = 0;

	if(ss.ss_family == AF_INET){
		struct sockaddr_in *sa = (struct sockaddr_in *)&ss;
		atype = ATYP_IPV4;
		write_ringbuff(&data->shared->client_out, &atype, 1);
		write_ringbuff(&data->shared->client_out, (uint8_t *)&sa->sin_addr, 4);
		write_ringbuff(&data->shared->client_out, (uint8_t *)&sa->sin_port, 2);
		if(ep_waiting_send(epoll_fd, data->shared->client_fd, data->shared->client_data) == -1){
			info->rep = REP_GENFAIL;
			reply_with_err(epoll_fd, data);
			return 0;
		}
	}
	else if(ss.ss_family == AF_INET6){
		struct sockaddr_in6 *sa = (struct sockaddr_in6 *)&ss;
		atype = ATYP_IPV6;
		write_ringbuff(&data->shared->client_out, &atype, 1);
		write_ringbuff(&data->shared->client_out, (uint8_t *)&sa->sin6_addr, 16);
		write_ringbuff(&data->shared->client_out, (uint8_t *)&sa->sin6_port, 2);
		if(ep_waiting_send(epoll_fd, data->shared->client_fd, data->shared->client_data) == -1){
			info->rep = REP_GENFAIL;
			reply_with_err(epoll_fd, data);
			return 0;
		}
	}
	else{
		info->rep = REP_GENFAIL;
		reply_with_err(epoll_fd, data);
		return 0;
	}

	data->shared->state = STATE_SENDING_REPLY_2;
	return 0;
}

int closed(int epoll_fd, Data *data){
	close(data->self_fd);
	ep_delete_fd(epoll_fd, data->self_fd);
	free_data(data);

	return 0;
}

int half_close(int epoll_fd, Data *data, uint32_t ev){
	data->shared->state = STATE_CLOSED;
	return 0;
}

int full(int epoll_fd, Data *data, uint32_t ev){
	if(ev & EPOLLOUT){
		int ret = send_traffic(data->self_fd, data->shared);
		if(ret == -1){
			data->shared->state = STATE_HALF_CLOSE;
		}
		if(ret == 0){
			if(ep_done_sending(epoll_fd, data->self_fd, data) == -1){
				data->shared->state = STATE_CLOSED;
			}
		}
		return 0;
	}
	if(ev & EPOLLIN){
		int ret = read_traffic(data->self_fd, data->shared);
		if(ret == -1){
			data->shared->state = STATE_HALF_CLOSE;
		}
		int pfd;
		Data *pdata;
		if(data->self_fd == data->shared->client_fd){
			pfd = data->shared->server_fd;
			pdata = data->shared->server_data;
		}
		else{
			pfd = data->shared->client_fd;
			pdata = data->shared->client_data;
		}
		if(ep_waiting_send(epoll_fd, pfd, pdata) == -1){
			return 0;
		}
	}

	return 0;
}

int full_udpa(int epoll_fd, Data *data, uint32_t ev){
return -1;
}

uint16_t socks5(int epoll_fd, struct epoll_event *event, Configs *configs){
	Data *data = event->data.ptr;
	if(!data->shared){
		log_error(L_NULLCHECKFAIL);
		return 0;
	}

	uint32_t ev = event->events;
	int ret = 0;
	switch(data->shared->state){ // These are not necessarily in order
		case STATE_NO_STATE: // I don't think this is possible
			break;
		case STATE_WAITING_METHODS:
			ret = waitingmethods(epoll_fd, data, configs, ev);
			break;
		case STATE_SENDING_METHOD:
			ret = sendingmethod(epoll_fd, data, ev);
			break;
		case STATE_AUTHENTICATING: // ??
			break;
		case STATE_WAITING_COMMAND:
			ret = waitingcommand(epoll_fd, data, configs, ev);
			break;
		case STATE_ASYNC_DNS: // This should never happen
			break;
		case STATE_SENDING_REPLY:
			ret = sendingreply(epoll_fd, data, ev);
			break;
		case STATE_SENDING_REPLY_2:
			ret = sendingreply(epoll_fd, data, ev);
			break;
		case STATE_CONNECTING:
			ret = connecting(epoll_fd, data, ev);
			break;
		case STATE_BIND_LISTENING:
			ret = bindlistening(epoll_fd, data, ev);
			break;
		case STATE_FULL:
			ret = full(epoll_fd, data, ev);
			break;
		case STATE_FULL_UDPA:
			ret = full_udpa(epoll_fd, data, ev);
			break;
		case STATE_HALF_CLOSE:
			ret = half_close(epoll_fd, data, ev);
			break;
		case STATE_CLOSED:
			ret = closed(epoll_fd, data);
			break;
		default:
			break;
	}

	return ret;
}
