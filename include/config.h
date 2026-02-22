#ifndef CONFIG_H
#define CONFIG_H

#define E_CONFIGOPEN 0x01
#define E_ADDRMALLOC 0x02
#define E_CNFNOADDRS 0x04
#define E_CNFNOPORTN 0x08
#define E_CNFNOA_LOG 0x10
#define E_CNFNOE_LOG 0x20
#define E_CNFNOMXCON 0x40
#define E_CNFNOMETHS 0x80

#define METH_NOAUTH 0x00
#define METH_GSSAPI 0x01
#define METH_USERPW 0x02
#define METH_NOMETH 0xFF

struct listen_addrs_s{
    struct addrinfo *next;
    struct addrinfo *addr;
};

struct configs_s{
    struct listen_addrs_s *addrs; // Linked list of addrinfo structs (which may be linked lists)
    uint16_t port; // Listen port
    char a_log[255]; // Path to access log
    char e_log[255]; // Path to error log
    int max_conns; // Maximum allowable connections
    uint8_t allow_domains; // Allow domain name requests
    uint8_t allow_connect;
    uint8_t allow_bind;
    uint8_t allow_udpassoc;
    uint8_t methods[255]; // Identifier octets for auth methods set to 1
};

uint16_t parse_configs(struct configs_s *configs);
void free_config_addrs(struct configs_s *configs);
int test_configs(void);

#endif