#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>

#include "firewall.h"
#include "config.h"
#include "epoll_data.h"
#include "socks.h"

int firewall_iton(Netmask *dst, int af, int mask){
	if(mask < 0){
		return -1;
	}

	if(af == AF_INET){
		if(mask > 32){
			return -1;
		}
		uint32_t netmask;
		memset(&netmask, 0xFF, sizeof(netmask));
		netmask = netmask << (32 - mask);
		dst->inet = netmask;
	}
	else if(af == AF_INET6){
		if(mask > 128){
			return -1;
		}
		uint8_t netmask[16];
		memset(&netmask, 0, sizeof(netmask));
		int bits_left = mask;
		for(int i = 0; i < sizeof(netmask); i++){
			netmask[i] = 0xFF;
			if(bits_left > 8){
				bits_left -= 8;
				continue;
			}
			netmask[i] = netmask[i] << (8 - bits_left);
			break;
		}
		memcpy(dst->inet6, netmask, sizeof(dst->inet6));
	}
	else{
		return -1;
	}

	return 0;
}

int mask_sin(uint32_t *dst, uint32_t *netmask){
	*dst = htonl((ntohl(*dst) & *netmask));

	return 0;
}

int mask_sin6(uint8_t *dst, int dst_sz, uint8_t *netmask){
	for(int i = 0; i < 16; i++){
		dst[i] = (dst[i] & netmask[i]);
	}

	return 0;
}

int check_sin(uint32_t addr, Blockaddr_ll *current){
	while(current){
		if(current->sa->sa_family != AF_INET){
			current = current->next;
			continue;
		}

		uint32_t mask_addr = addr;
		mask_sin(&mask_addr, &current->netmask.inet);
		struct sockaddr_in *sin = (struct sockaddr_in *)current->sa;
		if(mask_addr == sin->sin_addr.s_addr){
			return 1;
		}

		current = current->next;
	}

	return 0;
}

int check_sin6(uint8_t *addr, Blockaddr_ll *current){
	while(current){
		if(current->sa->sa_family != AF_INET6){
			current = current->next;
			continue;
		}

		uint8_t mask_addr[16];
		memcpy(mask_addr, addr, sizeof(mask_addr));
		mask_sin6(mask_addr, sizeof(mask_addr), current->netmask.inet6);
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)current->sa;
		if(memcmp(mask_addr, sin6->sin6_addr.s6_addr, sizeof(mask_addr)) == 0){
			return 1;
		}

		current = current->next;
	}

	return 0;
}

int check_ai(struct addrinfo *ai, Blockaddr_ll *block_head){
	struct addrinfo *current = ai;
	while(current){
		if(current->ai_family == AF_INET){
			struct sockaddr_in *sin = (struct sockaddr_in *)current->ai_addr;
			if(check_sin(sin->sin_addr.s_addr, block_head)){
				return 1;
			}
		}
		else if(current->ai_family == AF_INET6){
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)current->ai_addr;
			if(check_sin6(sin6->sin6_addr.s6_addr, block_head)){
				return 1;
			}
		}
		current = current->ai_next;
	}

	return 0;
}

int udpa_unspec(Info *info){
	if(info->cmd != CMD_UDPA || !info->sa){
		return 0;
	}

	if(info->sa->sa_family == AF_INET){
		struct sockaddr_in *sin = (struct sockaddr_in *)info->sa;
		if(sin->sin_addr.s_addr == 0){
			return 1;
		}
	}
	else if(info->sa->sa_family == AF_INET6){
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)info->sa;
		uint8_t zeros[16] = {0};
		if(memcmp(sin6->sin6_addr.s6_addr, zeros, 16) == 0){
			return 1;
		}
	}

	return 0;
}

int firewall(Info *info, Configs *configs){
	if(udpa_unspec(info)){
		return 0;
	}

	if(info->sa){
		if(info->sa->sa_family == AF_INET){
			struct sockaddr_in *sin = (struct sockaddr_in *)info->sa;
			return check_sin(sin->sin_addr.s_addr, configs->block_head);
		}
		else if(info->sa->sa_family == AF_INET6){
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)info->sa;
			uint8_t addr[16] = {0};
			memcpy(addr, sin6->sin6_addr.s6_addr, sizeof(addr));
			return check_sin6(addr, configs->block_head);
		}
	}
	else if(info->ai){
		return check_ai(info->ai, configs->block_head);
	}

	return 1;
}
