#ifndef FIREWALL_H
#define FIREWALL_H

typedef struct Data Data;
typedef struct Configs Configs;

int firewall(Data *data, Configs *configs);
int mask_sin(uint32_t *dst, int mask);
int mask_sin6(uint8_t *dst, int dst_sz, int mask);

#endif
