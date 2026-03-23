```mermaid
flowchart TD
	START --> configs["Parse configs subroutine"]
	configs --> logger["Start logger thread"]
	logger --> epoll["Start epoll instance"]
	epoll --> domains{"Domains names allowed?"}
	domains --> |no| listeners
	domains --> |yes|resolver["Start resolver thread"]
	listeners["Start listeners"]
	resolver --> listeners
	listeners --> loop["Main event loop"]

```
---
```mermaid
sequenceDiagram
	participant C as Client
	participant P as Proxy
	participant S as Server
	C->>P: TCP Handshake
	C->>P: Method Request
	P->>C: Method Response
	C<<-->>P: Subnegotiation
	C->>P: SOCKS Request
	P->>S: TCP Handshake
	P->>C: SOCKS Response
	C<<-->>S: Forward Traffic
```
```mermaid
sequenceDiagram
	participant C as Client
	participant P as Proxy
	participant S as Server
	C<<->>P: TCP Handshake
	C->>P: Method Request
	P->>C: Method Response
	C<<-->>P: Subnegotiation
	C->>P: SOCKS Request
	P->>P: Create Listener
	P->>C: SOCKS Response
	S->>P: TCP Handshake
	P->>C: SOCKS Response
	C<<-->>S: Forward Traffic
```

```mermaid
sequenceDiagram
	participant C as Client
	participant P as Proxy
	participant S as Server
	C<<->>P: TCP Handshake
	C->>P: Method Request
	P->>C: Method Response
	C<<-->>P: Subnegotiation
	C->>P: SOCKS Request
	P->>P: Create UDP Port
	P->>C: SOCKS Response
	C<<-->>P: TCP Control Channel
	C<<-->>S: Relay Traffic
```
---

no state
resolver
listening
waiting methods
sending methods
authenticating
waiting command
async dns
sending reply
sending reply 2
connecting
bind listening
full
full udpa
half close
closed
