#ifndef FIREWALL_H
#define FIREWALL_H

typedef struct Info Info;
typedef struct Configs Configs;
typedef union Netmask Netmask;

int firewall(Info *info, Configs *configs);
int firewall_iton(Netmask *dst, int af, int mask);
int mask_sin(uint32_t *dst, uint32_t *netmask);
int mask_sin6(uint8_t *dst, int dst_sz, uint8_t *netmask);

#endif
