#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_FILE "/etc/socksprox.conf"

#define METH_NOAUTH 0x00
#define METH_GSSAPI 0x01
#define METH_USERPW 0x02
#define METH_NOMETH 0xFF

typedef union Netmask{
	uint32_t inet;
	uint8_t inet6[16];
} Netmask;

typedef struct Sockaddr_ll{
	struct Sockaddr_ll *next;
	int addrlen;
	struct sockaddr *sa;
} Sockaddr_ll;

typedef struct Blockaddr_ll{
	union Netmask netmask;
	struct sockaddr *sa;
	struct Blockaddr_ll *next;
} Blockaddr_ll;

typedef struct Configs{
	Sockaddr_ll *listen_head; // Linked list of addresses to listen on
	char a_log[255]; // Path to access log
	char e_log[255]; // Path to error log
	int max_conns; // Maximum allowable connections
	uint8_t allow_domains; // Allow domain name requests
	uint8_t allow_connect;
	uint8_t allow_bind;
	uint8_t allow_udpassoc;
	uint8_t methods[255]; // Identifier octets for auth methods set to 1
	Sockaddr_ll *bind_advertise;
	Sockaddr_ll *bind_listen;
	Sockaddr_ll *udpa_advertise;
	Sockaddr_ll *udpa_listen;
	Blockaddr_ll *block_head;
} Configs;

int parse_configs(Configs *configs);
void free_configs(Configs *configs);

#endif