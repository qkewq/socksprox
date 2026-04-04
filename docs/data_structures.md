# Data Structures

Explanations of the data structures that the server uses to store connection information internally.

## Table of Contents
- [Epoll Data](#epoll-data)
  - [Data](#data)
  - [Shared](#shared)
  - [Info](#info)
  - [Ringbuff](#ringbuff)
- [Config](#config)
  - [Configs](#configs)
  - [Sockaddr_ll](#sockaddr_ll)
  - [Blockaddr_ll](#blockaddr_ll)


## Epoll Data

`epoll_data` is the core of the server.  It stores things like the state, relevent file dscriptors, and connection information.  It is also the link between a client connection and the proxied connection.  The structures are defined in `include/epoll_data.h`.

#### Data

Every file descriptor created by the server is placed inside of a `Data struct`.  A pointer to this struct is given to the epoll instance and is returned whenever read or write operations are ready to be performed.
```C
typedef struct Data{
	uint8_t type;
	int self_fd;
	struct Shared *shared;
} Data;
```
|Type|Note|
|--|--|
|TYPE_LISTENER (`0x00`)|The listener type means `self_fd` is one of the server listeners where new clients connect, the pointer to `shared` is NULL.|
|TYPE_PEER (`0x01`)|The peer type means `self_fd` is a client or proxied connection|
|TYPE_RESOLVER (`0x02`)|The resolver type means `self_fd` is the file descriptor that the `resolver` will use to output DNS results, the pointer to `shared` is NULL.|

#### Shared

The `Shared struct` is shared between a client and proxied connection and is what links the two together.  Meaning a pointer to a single `Shared struct` is in two separate `Data structs`.
```C
typedef struct Shared{
	uint8_t state;
	int client_fd;
	int server_fd;
	struct Ringbuff client_out;
	struct Ringbuff server_out;
	struct Data *client_data;
	struct Data *server_data;
	uint8_t client_flags;
	uint8_t server_flags;
	struct Alog *access;
	struct Info *info;
	void *ptr;
} Shared;
```
The state field is the most important field as it tracks the state of the connection throughout the handshake and proxy process ([See States](state_machine.md)).  The other fields contain both client and server (the proxied connection) file descriptors and pointers to their respective `Data structs`.  It also contains pointers to their output buffers ([See Ringbuffs](#ringbuff)) and a pointer to the shared `Info struct` ([See Info](#info)).  The `access` field is a pointer to an `Alog` struct wich contains the IP and port of the client and proxied connection, this is given to the logger to write access logs.  The `server_out` field acts as a temporary input buffer for the client during the socks handshake.  The client and server flags are used to track TCP half close states.  The void pointer `ptr` is used for authentication method spceific data.
|Flag|Note|
|--|--|
|FLAG_READ_CLOSED (`0x01`)|This side of the connection recieved `EOF`|
|FLAG_WRITE_CLOSED (`0x02`)|Either recieved `SIGPIPE` or read was closed|
|FLAG_FLUSHING (`0x04`)|Outbuffer is flushing prior to closing write|

#### Info

The `Info struct` contains information about the socks handshake and request made by the client.
```C
typedef struct Info{
	uint8_t method;
	uint8_t rep;
	uint8_t cmd;
	uint8_t atyp;
	int resolver_ret;
	int ai_index;
	struct addrinfo *ai;
	struct sockaddr *sa;
} Info;
```
The `method` field is the authentication method that the client used to connect to the server.  The `rep` field is the server reply to the socks request.  The `atyp` field is the address type of the address sent by the client (IPv4, v6, or domain name).  The `resolver_ret` field is the value returned by the DNS resolver if the address type was domain name.  The `ai_index` field contains the current index of the addrinfo list being used.  For example, if a domain name resolved to multiple addresses and a connection to the first one failed, the server will set `ai_index` to `1` and attempt to connect using the second address in the list.  The `ai` and `sa` fields contain the address sent by the client in the socks request.  If the client sent an IPv4 or v6 address then `sa` will contain a pointer to a sockaddr struct with the address and port.  If the client sent a domain name, `ai` will contain a pointer to an addrinfo struct with the list of returned IP address.  Either `sa` or `ai` will always be NULL but never both.

#### Ringbuff

Ring buffers are used to store network traffic to the client or server before they are sent over the network.  When traffic is recieved destined for the client or server it is placed in the respective ring buffer and the correspodning file descriptor is registered with `EPOLLOUT`.  When the outgoing FD is returned, data is read from the buffer and written to the socket.
```C
typedef struct Ringbuff{
	uint8_t *buff;
	size_t readhead;
	size_t writehead;
	size_t capacity;
	size_t used;
} Ringbuff;
```
The `buff` field contains a pointer to an array whose size is defined by `OUTBUFFSIZE`, currently 4096 bytes.  The `readhead` and `writehead` fields are the current index of the read and write head respectively.  The `capacity` field is the buffers total capacity, i.e. `OUTBUFFSIZE` and `used` is the number of bytes currently in use.

## Config

The config parser utilizes `Configs struct` to store configurations thoughout the runtime of the server.

#### Configs

The `Configs struct` has all of the user defined settings and is popluated at startup.
```C
typedef struct Configs{
	Sockaddr_ll *listen_head;
	char a_log[255];
	char e_log[255];
	int max_conns;
	uint8_t allow_domains;
	uint8_t allow_connect;
	uint8_t allow_bind;
	uint8_t allow_udpassoc;
	uint8_t methods[255];
	Sockaddr_ll *bind_advertise;
	Sockaddr_ll *bind_listen;
	Sockaddr_ll *udpa_advertise;
	Sockaddr_ll *udpa_listen;
	Blockaddr_ll *block_head;
} Configs;
```
|Field|Note
|--|--|
|listen_head|Pointer to a linked list contaning the IP addresses and ports for the server to listen on|
|a_log|File path to where the server should write access logs|
|e_log|File path to where the server should write error logs|
|max_cons|The maximum number of connections, to be given to the epoll instance|
|allow_domains|Is the client allowed to send domains|
|allow_connect|Is the client allowed to use the connect command|
|allow_bind|Is the client allowed to use the bind command|
|allow_udpassoc|Is the client allowed to use the udp associate command|
|methods|Array of booleans each corresponding to the repective method specifier|
|bind_advertise|Pointer to a linked list of addresses the server should advertise when binding|
|bind_listen|Pointer to a linked list of addresses the server should actually use when binding|
|udpa_advertise|Pointer to a linked list of addresses the server should advertise when creating udp relay port|
|udpa_listen|Pointer to a linked list of addresses the server should used actually use when creating udp relay port|
|block_head|Pointer to a linked list of blocked networks|

For the `methods` field, the index of each element corresponds to the assigned method specifier, for example, if the user allows `No Authentication` then the byte at index `0x00` will be set to 1 to indicate it is allowed.
|Method|Note|
|--|--|
|METH_NOAUTH (`0x00`)|No authentication is required to connect to the server|
|METH_GSSAPI (`0x01`)|GSSAPI authentication (not supported)|
|METH_USERPW (`0x02`)|Username and password authentication (Not supported yet)|
|METH_NOMETH (`0xFF`)|Used to indicate to the client that no acceptable methods were presented to the server|

The reasoning for using linked lists for the addresses is to combine both IPv4 and v6 into the same list, address family is determined per list element at search time.  All of the linked lists use the `Sockaddr_ll struct` with the exception of `block_head` which uses the `Blockaddr_ll struct`.

#### Sockaddr_ll

The `Sockaddr_ll struct` is a linked list that is used to store IP addresses and port numbers.  It is address family agnostic.
```C
typedef struct Sockaddr_ll{
	struct Sockaddr_ll *next;
	int addrlen;
	struct sockaddr *sa;
} Sockaddr_ll;
```
The `next` field is used to point to the next element in the list.  The `addrlen` field contains the length of the `sockaddr struct` pointed to by `sa`.

#### Blockaddr_ll

The `Blockaddr_ll struct` is a linked list used to store the networks that the user has specified for the built in firewall to block.
```C
typedef struct Blockaddr_ll{
	union Netmask netmask;
	struct sockaddr *sa;
	struct Blockaddr_ll *next;
} Blockaddr_ll;
```
The `next` field contains a pointer to the next element in the list.  The `sa` field contains a `sockaddr struct` that has the blocked network.  The network stored in `sa` is already masked with the netmask stored in `netmask`.  The `netmask` field is a union that contains either a 4 byte IPv4 netmask or a 16 byte IPv6 netmask.
```C
typedef union Netmask{
	uint32_t inet;
	uint8_t inet6[16];
} Netmask;
```

