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

#define SUCCESS         0x00
#define RECV_EOF        0x01
#define RECV_ERR        0x02
#define VER_ERR         0x03
#define NULLCHK_ERR     0x04
#define MALLOC_ERR      0x05
#define EPOLLCTL_ERR    0x06
#define GENERAL_ERR     0x07
#define SEND_ERR        0x08
#define METHINVAL       0x09
#define BUFFRD_ERR      0x0A
#define BUFFWR_ERR      0x0B
#define COMM_ERR        0x0C
#define GAI_ERR         0x0D
#define CLOSECONN       0x0E
#define GSO_ERR         0x0F
#define GSN_ERR         0x10
#define NOLISADDRS      0x11
#define SOCKOPEN_ERR    0x12
#define SETSOCKOPT_ERR  0x13
#define BIND_ERR        0x14
#define ACCEPT_ERR      0x15
#define BADTYPE         0x16
#define BADSTATE        0x17

struct epoll_data_s;
struct epoll_event;
struct configs_s;
uint16_t socks5(int epoll_fd, struct epoll_event *event, struct configs_s *configs);
uint16_t freeclose(int epoll_fd, struct epoll_event *events);

#endif