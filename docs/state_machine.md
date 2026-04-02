# The State Machine

The server keeps track of connections by tracking their state in the SOCKS handshake or proxying process.  Each set of two file descriptors (one client and one server) have a shared state that is stored in their `Shared struct` ([See Data Structures](data_structures.md)).

## States

|State|Note|
|--|--|
|STATE_NO_STATE (`0x00`)|Stateless, this is currently impossible to reach|
|STATE_WAITING_METHODS (`0x01`)|A new client has connected and socksprox is waiting to receive the SOCKS `method request`|
|STATE_SENDING_METHOD (`0x02`)|Socksprox is sending the `method response`|
|STATE_AUTHENTICATING (`0x03`)|A method that requires a sub-negotiation was selected|
|STATE_WAITING_COMMAND (`0x04`)|The client is authenticated and socksprox is waiting for the `SOCKS request`|
|STATE_ASYNC_DNS (`0x05`)|The cleint sent an address with type `Domain Name` and socksprox is doing an asynchronous DNS query|
|STATE_SENDING_REPLY (`0x06`)|Socksprox is sending the `SOCKS reply` to the client|
|STATE_SENDING_REPLY_2 (`0x07`)|Socksprox is sending the second `SOCKS reply` in the case of the `bind` command|
|STATE_CONNECTING (`0x08`)|Socksprox is waiting for an outbound connection to success or fail, in the case of the `connect` command|
|STATE_BIND_LISTENING (`0x09`)|Socksprox is waiting for a server to conect to it's proxied listener, in the case of the `bind` command|
|STATE_FULL (`0x0A`)|A full connection has been achieved and socksprox is now forwarding traffic between client and server|
|STATE_FULL_UDPA (`0x0B`)|A UDP relay port has been established and is relaying traffic, in the case of the `UDP Associate` command|
|STATE_HALF_CLOSE (`0x0C`)|Read or write end of either connection has been closed|
|STATE_CLOSED (`0x0D`)|Connection is closed, file descriptors are being closed and data is being cleared|

Most of the states can be divided based on whether they are tracking input or output.
```
Input: Waiting methods, waiting command, bind listening
Output: Sending methods, sending reply, sending reply 2, connecting
```
For states where input is expected the file descriptor is registered with `EPOLLIN` in order to be notified when input is read.  For output state file descriptors are registered with `EPOLLOUT` until sending is complete.  For full states where input and output is expected, file descriptors remain registered with `EPOLLIN` and are modified with `EPOLLOUT` when output is waiting to be sent.

The majority of states are updated by the `socks5` function defined in `src/socks.c` and called in `src/socksprox.c`.  `socks5` is made public by the header file however, each state has a private function within `socks.c` that handles that state.  Each state function has "responsibility" over the data structures it handles and it passes off responsibilty when it updates the state.  The state functions also "prepare" the data and file descriptors for the next state, for example, the `waitingmethods` function will register the file descriptor with `EPOLLOUT` before changing state to `STATE_SENDING_METHODS` and handing responsibility to the `sendingmethods` function.

The `closed` function that handles `STATE_CLOSED` is ultimately responsible for freeing memory and closing file desrciptors.
