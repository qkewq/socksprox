#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>

#include "config.h"
#include "firewall.h"

int yes_check(char *val){
	if(strcmp(val, "yes") == 0){
		return 1;
	}
	return 0;
}

char *yesno(uint8_t val){
	if(val == 1){
		return "yes";
	}
	return "no";
}

int method_parse(char *val){
	if(strcmp(val, "no-auth") == 0){
		return METH_NOAUTH;
	}
	else if(strcmp(val, "gssapi") == 0){
		return METH_GSSAPI;
	}
	else if(strcmp(val, "username-password") == 0){
		return METH_USERPW;
	}
	return -1;
}

int conf_error_check(Configs *configs){
	int ret = 0;
	if(!configs->listen_head){
		ret = 1;
		dprintf(STDERR_FILENO, "No addresses were provided\n");
	}
	if(configs->a_log[0] == 0){
		ret = 1;
		dprintf(STDERR_FILENO, "No path to access log provided\n");
	}
	if(configs->e_log[0] == 0){
		ret = 1;
		dprintf(STDERR_FILENO, "No path to error log provided\n");
	}
	if(configs->max_conns < 1){
		ret = 1;
		dprintf(STDERR_FILENO, "Invalid maximum connections\n");
	}
	if(!configs->allow_connect && !configs->allow_bind && !configs->allow_udpassoc){
		ret = 1;
		dprintf(STDERR_FILENO, "No commands allowed, exitting\n");
	}
	if(configs->allow_bind){
		if(!configs->bind_advertise){
			ret = 1;
			dprintf(STDERR_FILENO, "Bind was allowed but no advertise adress was provided\n");
		}
		if(!configs->bind_listen){
			ret = 1;
			dprintf(STDERR_FILENO, "Bind was allowed but no listen address was provided\n");
		}
	}
	if(configs->allow_udpassoc){
		if(!configs->udpa_advertise){
			ret = 1;
			dprintf(STDERR_FILENO, "UDP Associate was allowed but no advertise address was provided\n");
		}
		if(!configs->udpa_listen){
			ret = 1;
			dprintf(STDERR_FILENO, "UDP Associate was allowed but no listen address was provided\n");
		}
	}
	int nmethods = 0;
	for(int i = 0; i < 255; i++){
		nmethods += configs->methods[i];
	}
	if(!nmethods){
		ret = 1;
		dprintf(STDERR_FILENO, "No methods were provided\n");
	}

	return ret;
}

char *skip_whitespace(char *c){
	while(*c == ' ' || *c == '\t'){
		c++;
	}

	return c;
}

int line_comment(char *c){
	if(*c == '\n' || *c == '\r' || *c == '\0' || *c == '#' || *c == ';'){
		return 1;
	}

	return 0;
}

char *get_key(char *c, char *key, int keysz){
	for(int i = 0; i < keysz; i++){
		key[i] = *c;
		c++;
		if(*c == ' ' || *c == '\t' || *c == '='){
			key[i + 1] = '\0';
			break;
		}
	}

	return c;
}

char *to_value(char *c){
	while(*c == ' ' || *c == '\t' || *c == '='){
		c++;
	}

	return c;
}

int new_line(char *c){
	if(*c == '\n' || *c == '\r' || *c == '\0'){
		return 1;
	}

	return 0;
}

char *get_val(char *c, char *val, int valsz){
	for(int i = 0; i < valsz; i++){
		val[i] = *c;
		c++;
		if(*c == '\n' || *c == '\r' || *c == '\0' || *c == ' ' || *c == '\t'){
			val[i + 1] = '\0';
			break;
		}
	}

	return c;
}

int delim_ind(char delim, char *val, int valsz){
	for(int i = 0; i < valsz; i++){
		if(val[i] == delim){
			return i;
		}
	}

	return 0;
}

struct addrinfo *addr_only_gai(char *val, int valsz){
	char addr[39] = {0};
	if(valsz > sizeof(addr)){
		return NULL;
	}
	memcpy(&addr, val, valsz);
	struct addrinfo *res = malloc(sizeof(struct addrinfo));
	if(!res){
		return NULL;
	}
	if(getaddrinfo(addr, NULL, NULL, &res) != 0){
		return NULL;
	}

	return res;
}

int link_addrs_from_gai(struct addrinfo *ai, Sockaddr_ll **head){
	if(!ai){
		return -1;
	}

	struct addrinfo *ai_current = ai;
	while(ai_current){
		Sockaddr_ll *new_node = malloc(sizeof(Sockaddr_ll));
		struct sockaddr *sa = malloc(sizeof(struct sockaddr_storage));
		if(!new_node || !sa){
			return -1;
		}
		memcpy(sa, ai_current->ai_addr, ai_current->ai_addrlen);
		new_node->sa = sa;
		new_node->addrlen = ai_current->ai_addrlen;
		new_node->next = *head;
		*head = new_node;
		ai_current = ai_current->ai_next;
	}

	freeaddrinfo(ai);
	return 0;
}

int link_addr(char *addrport, int addrportsz, Sockaddr_ll **head){
	struct addrinfo hints;
	struct addrinfo *result;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	int delimind = delim_ind('_', addrport, addrportsz);
	char addr[39] = {0};
	char port[6] = {0};
	memcpy(addr, addrport, delimind);
	memcpy(port, &addrport[delimind + 1], addrportsz - delimind);

	int gai_ret = getaddrinfo(addr, port, &hints, &result);
	if(gai_ret != 0){
		return -1;
	}

	struct addrinfo *ai_current = result;
	while(ai_current){
		Sockaddr_ll *new_node = malloc(sizeof(Sockaddr_ll));
		struct sockaddr *sa = malloc(sizeof(struct sockaddr_storage));
		if(!new_node || !sa){
			return -1;
		}
		memcpy(sa, ai_current->ai_addr, ai_current->ai_addrlen);
		new_node->sa = sa;
		new_node->addrlen = ai_current->ai_addrlen;
		new_node->next = *head;
		*head = new_node;
		ai_current = ai_current->ai_next;
	}

	freeaddrinfo(result);
	return 0;
}

int link_blocks(struct addrinfo *ai, int mask, Blockaddr_ll **head){
	if(!ai){
		return -1;
	}

	// Should only be one address in the addrinfo struct for block addrs
	Blockaddr_ll *new_node = malloc(sizeof(Blockaddr_ll));
	struct sockaddr *sa = malloc(sizeof(struct sockaddr_storage));
	if(!new_node || !sa){
		return -1;
	}

	if(ai->ai_family == AF_INET){
		struct sockaddr_in *sin  = (struct sockaddr_in *)ai->ai_addr;
		mask_sin(&sin->sin_addr, mask);
	}
	else if(ai->ai_family == AF_INET6){
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ai->ai_addr;
		mask_sin6(&sin6->sin6_addr, 16, mask);
	}

	memcpy(sa, ai->ai_addr, ai->ai_addrlen);
	new_node->mask = mask;
	new_node->sa = sa;
	new_node->next = *head;
	*head = new_node;

	freeaddrinfo(ai);
	return 0;
}

int store_config(char *key, char *val, Configs *configs){
	if(strcmp(key, "access-log") == 0){
		memcpy(configs->a_log, val, sizeof(configs->a_log));
	}
	else if(strcmp(key, "error-log") == 0){
		memcpy(configs->e_log, val, sizeof(configs->e_log));
	}
	else if(strcmp(key, "max-connections") == 0){
		configs->max_conns = atoi(val);
	}
	else if(strcmp(key, "allow-domains") == 0){
		configs->allow_domains = yes_check(val);
	}
	else if(strcmp(key, "allow-connect") == 0){
		configs->allow_connect = yes_check(val);
	}
	else if(strcmp(key, "allow-bind") == 0){
		configs->allow_bind = yes_check(val);
	}
	else if(strcmp(key, "allow-udp-associate") == 0){
		configs->allow_udpassoc = yes_check(val);
	}
	else if(strcmp(key, "method") == 0){
		int meth = method_parse(val);
		if(meth != -1){
			configs->methods[meth] = 1;
		}
	}
	else if(strcmp(key, "bind-advertise-address") == 0){
		struct addrinfo *ai = addr_only_gai(val, strlen(val)); // Null check in link addrs
		if(link_addrs_from_gai(ai, &configs->bind_advertise) != 0){
			return -1;
		}
	}
	else if(strcmp(key, "bind-listen-address") == 0){
		struct addrinfo *ai = addr_only_gai(val, strlen(val));
		if(link_addrs_from_gai(ai, &configs->bind_listen) != 0){
			return -1;
		}
	}
	else if(strcmp(key, "udp-advertise-address") == 0){
		struct addrinfo *ai = addr_only_gai(val, strlen(val));
		if(link_addrs_from_gai(ai, &configs->udpa_advertise) != 0){
			return -1;
		}
	}
	else if(strcmp(key, "udp-listen-address") == 0){
		struct addrinfo *ai = addr_only_gai(val, strlen(val));
		if(link_addrs_from_gai(ai, &configs->udpa_listen) != 0){
			return -1;
		}
	}
	else if(strcmp(key, "listen") == 0){
		if(link_addr(val, strlen(val), &configs->listen_head) != 0){
			return -1;
		}
	}
	else if(strcmp(key, "block") == 0){
		int delimind = delim_ind('/', val, strlen(val));
		struct addrinfo *ai = addr_only_gai(val, delimind);
		if(!ai){
			return -1;
		}
		if(link_blocks(ai, atoi(&val[delimind + 1]), &configs->block_head) != 0){
			return -1;
		}
	}

	return 0;
}

int parse_configs(Configs *configs){ // Prints fatal errors to STDERR
	if(!configs){
		return 1;
	}
	memset(configs, 0, sizeof(*configs));

	FILE *file = fopen(CONFIG_FILE, "r");
	if(file == NULL){
		dprintf(STDERR_FILENO, "Error: Could not find config file at %s\n", CONFIG_FILE);
		return 1;
	}

	char line[255];
	while(fgets(line, sizeof(line), file) != NULL){
		char *c = line; // First char in line
		char key[64] = {0};
		char val[255] = {0};

		c = skip_whitespace(c);
		if(line_comment(c)){
			continue;
		}
		c = get_key(c, key, sizeof(key));
		c = to_value(c);
		if(new_line(c)){
			continue;
		}
		c = get_val(c, val, sizeof(val));

		if(store_config(key, val, configs) != 0){
			dprintf(STDERR_FILENO, "Error while allocating memory for configs\n");
			fclose(file);
			return 1;
		}
	}

	fclose(file);
	return conf_error_check(configs);
}

void free_sa(Sockaddr_ll *current){
	Sockaddr_ll *next = NULL;
	while(current){
		next = current->next;
		free(current);
		current = next;
	}
}

void free_blocks(Blockaddr_ll *current){
	Blockaddr_ll *next = NULL;
	while(current){
		next = current->next;
		free(current);
		current = next;
	}
}

void free_configs(Configs *configs){
	if(!configs){
		return;
	}
	Sockaddr_ll *current = NULL;
	current = configs->listen_head;
	if(current){
		free_sa(current);
		configs->listen_head = NULL;
	}
	current = configs->bind_advertise;
	if(current){
		free_sa(current);
		configs->bind_advertise = NULL;
	}
	current = configs->bind_listen;
	if(current){
		free_sa(current);
		configs->bind_listen = NULL;
	}
	current = configs->udpa_advertise;
	if(current){
		free_sa(current);
		configs->udpa_advertise = NULL;
	}
	current = configs->udpa_listen;
	if(current){
		free_sa(current);
		configs->udpa_listen = NULL;
	}

	Blockaddr_ll *block_head = NULL;
	block_head = configs->block_head;
	if(block_head){
		free_blocks(block_head);
		configs->block_head = NULL;
	}
}
