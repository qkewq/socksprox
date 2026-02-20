#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include "config.h"

#define METH_NOAUTH 0x00
#define METH_GSSAPI 0x01
#define METH_USERPW 0x02

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

int link_addr(struct configs_s *configs, char *val, char *port){
    struct addrinfo hints;
    struct addinfo *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int gai_ret = getaddrinfo(val, port, &hints, &result);
    if(gai_ret != 0){
        return 0;
    }
    struct listen_addrs_s *new_node = malloc(sizeof(struct listen_addrs_s));
    if(new_node == NULL){
        return E_ADDRMALLOC;
    }
    new_node->addr = result;
    new_node->next = configs->addrs;
    configs->addrs = new_node

    return 0;
}

uint16_t conf_error_check(struct configs_s *configs){
    uint16_t conf_error = 0;
    if(configs->addrs == NULL){
        conf_error |= E_CNFNOADDRS;
    }
    if(configs->port < 1 || configs->port > 65535){
        conf_error |= E_CNFNOPORTN;
    }
    if(configs->a_log[0] == 0){
        conf_error |= E_CNFNOA_LOG;
    }
    if(configs->e_log[0] == 0){
        conf_error |= E_CNFNOE_LOG;
    }
    if(configs->max_conns < 1){
        conf_error |= E_CNFNOMXCON;
    }
    for(int i = 0; i < sizeof(configs->methods); i++){
        if(configs->methods[i] == 1){
            break;
        }
        conf_error |= E_CNFNOMETHS;
    }

    return conf_error;
}

uint16_t parse_configs(struct configs_s *configs){
    memset(configs, 0, sizeof(*configs));
    configs->addrs = NULL;
    FILE *file = fopen("/etc/socksprox.conf", "r");
    if(file == NULL){
        return E_CONFIGOPEN;
    }
    
    char line[255];
    char port_text[6];
    while(fgets(line, sizeof(line), file) != NULL){
        char *c = line;
        char key[32] = {0};
        char val[255] = {0};
        while(*c == ' ' || *c == '\t'){
            c++;
        }
        if(*c == '\n' || *c == '\r' || *c == '\0' || *c == '#' || *c == ';'){
            continue;
        }

        for(int i = 0; i < sizeof(key); i++){
            key[i] = *c;
            c++;
            if(*c == ' ' || *c == '\t' || *c == '='){
                key[i + 1] = '\0';
                break;
            }
        }

        while(*c == ' ' || *c == '\t' || *c == '='){
            c++;
        }
        if(*c == '\n' || *c == '\r' || *c == '\0'){
            continue;
        }

        for(int i = 0; i < sizeof(val); i++){
            val[i] = *c;
            c++;
            if(*c == '\n' || *c == '\r' || *c == '\0'){
                val[i + 1] = '\0';
                break;
            }
        }

        if(strcmp(key, "access-log") == 0){
            memcpy(configs->a_log, val, sizeof(configs->a_log));
        }
        else if(strcmp(key, "error-log") == 0){
            memcpy(configs->e_log, val, sizeof(configs->e_log));
        }
        else if(strcmp(key, "listen-port") == 0){
            memcpy(&port_text, val, sizeof(port_text));
            configs->port = atoi(val);
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
        else if(strcmp(key, "allow-udp-associate") == 0){
            configs->allow_udpassoc = yes_check(val);
        }
        else if(strcmp(key, "method") == 0){
            int meth = method_parse(val);
            if(meth != -1){
                configs->methods[meth] = 1;
            }
        }
        else if(strcmp(key, "listen") == 0){
            if(link_addr(configs, val, port_text) == E_ADDRMALLOC){
                return E_ADDRMALLOC;
            }
        }
    }

    return conf_error_check(configs);
}

void free_config_addrs(struct configs_s *configs){
    struct listen_addrs_s *current = configs->addrs;
    while(current != NULL){
        freeaddrinfo(current->addr);
        struct listen_addrs_s *next_node = current->next;
        free(current);
        current = next_node;
    }
    configs->addrs = NULL;
}

int test_configs(void){
    struct configs_s configs;
    printf("Checking configs...\n");
    uint16_t ret = parse_configs(&configs);
    if(ret != 0){
        printf("FOUND ERRORS:\n");
        if(ret & E_CONFIGOPEN){printf("\tCOULDN'T OPEN CONFIG FILE\n");};
        if(ret & E_ADDRMALLOC){printf("\tERROR ALLOCATING MEMORY\n");};
        if(ret & E_CNFNOADDRS){printf("\tNO LISTEN ADDRESSES\n");};
        if(ret & E_CNFNOPORTN){printf("\tNO OR INVALID PORT NUMBER\n");};
        if(ret & E_CNFNOA_LOG){printf("\tNO PATH TO ACCESS LOG\n");};
        if(ret & E_CNFNOE_LOG){printf("\tNO PATH TO ERROR LOG\n");};
        if(ret & E_CNFNOMXCON){printf("\tNO OR INVALID MAXIMUM CONNECTIONS\n");};
        if(ret & E_CNFNOMETHS){printf("\tNO AUTHENTICATION METHODS\n");};
        printf("\n");
    }

    printf("Configs:\n");
    if(ret & E_CONFIGOPEN || ret & E_ADDRMALLOC){
        printf("ERROR WAS FATAL\n");
        return 0;
    }
    printf("Listening addresses:\n");
    struct listen_addrs_s *current = configs.addrs;
    if(current == NULL){
        printf("\tNO ADDRESSES\n");
    }
    while(current != NULL){
        printf("\t%s\n", current->addr);
        current = current->next;
    }
    printf("Listening on port: ");
    if(ret & E_CNFNOPORTN){
        printf("error\n");
    }
    else{
        printf("%d\n", configs.port);
    }
    printf("Access log path: ");
    if(ret & E_CNFNOA_LOG){
        printf("error\n");
    }
    else{
        printf("%s\n", configs.a_log);
    }
    printf("Error log path: ");
    if(ret & E_CNFNOE_LOG){
        printf("error\n");
    }
    else{
        printf("%s\n", configs.e_log);
    }
    printf("Maximum connections: ");
    if(ret & E_CNFNOMXCON){
        printf("error\n");
    }
    else{
        printf("%d\n", configs.max_conns);
    }
    printf("Method Identifiers:\n");
    if(ret & E_CNFNOMETHS){
        printf("\terror: no methods specified\n");
    }
    else{
        for(int i = 0; i < sizeof(configs.methods); i++){
            if(configs.methods[i] == 1){
                printf("\t0x%02x\n", i);
            }
        }
    }
    printf("Allow domain names: %s\n", yesno(configs.allow_domains));
    printf("Allow connect command: %s\n", yesno(configs.allow_connect));
    printf("Allow bind command: %s\n", yesno(configs.allow_bind));
    printf("Allow udp associate command: %s\n", yesno(configs.allow_udpassoc));
    printf("\nEnd Configs\n");

    return 0;
}
