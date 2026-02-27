#ifndef SOCKS_H
#define SOCKS_H

#define ATYP_IPV4 0x01
#define ATYP_DOMN 0x03
#define ATYP_IPV6 0x04

#define CMD_CONN 0x01
#define CMD_BIND 0x02
#define CMD_UDPA 0x03

#define REP_SUCCESS 0x00
#define REP_GENFAIL 0x01
#define REP_RULESET 0x02
#define REP_NUNRECH 0x03
#define REP_HUNRECH 0x04
#define REP_REFUSED 0x05
#define REP_TTLEXPR 0x06
#define REP_BADCOMM 0x07
#define REP_BADATYP 0x08

struct epoll_data_s;
struct epoll_event;
struct configs_s;
uint16_t socks5(int epoll_fd, struct epoll_event *event, struct configs_s *configs);

#endif