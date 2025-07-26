# rdma_server_client
An example rdma server client to measure write and read bandwidth between two nodes with data consistency checks

```
Compile server:
    gcc -o rdma_server rdma_server.c -lrdmacm -libverbs -lssl -lcrypto

Compile client:
    gcc -o rdma_client rdma_client.c -lrdmacm -libverbs -lssl -lcrypto

Spawn server:
    ./rdma_server <server_ip>

Spawn client:
    ./rdma_client <server ip>

Example output:

Server:
$ ./rdma_server 192.168.1.2
Waiting for connection from client...
received event: 4
received event: RDMA_CM_EVENT_ESTABLISHED
received expected event: RDMA_CM_EVENT_ESTABLISHED
Connection accepted from 192.168.1.5
received success for opcode 128 at WC idx 0
Client requesting buffer of length: 1073741824
MD5: d277f4a3c1df17fbfc330ed1aeb55981
received success for opcode 0 at WC idx 0
received event: RDMA_CM_EVENT_DISCONNECTED
received expected event: RDMA_CM_EVENT_DISCONNECTED
MD5: a745de29031410798d514fceb71ae3e7
Waiting for connection from client...

Client:
$ ./rdma_client 192.168.1.2
received event: RDMA_CM_EVENT_ADDR_RESOLVED
received expected event: RDMA_CM_EVENT_ADDR_RESOLVED
received event: RDMA_CM_EVENT_ROUTE_RESOLVED
received expected event: RDMA_CM_EVENT_ROUTE_RESOLVED
received event: RDMA_CM_EVENT_ESTABLISHED
received expected event: RDMA_CM_EVENT_ESTABLISHED
Connection accepted by 192.168.1.2
MD5: 4c2df7bca511b4d55f2abbbf852a789c
received success for opcode 0 at WC idx 0
received success for opcode 128 at WC idx 1
received success for opcode 1 at WC idx 0
RDMA WR BW: 388.530097 gbps
Randomize Local Buffer
MD5: 1ec73bce52dfb51a9d234b09c8376d13
received success for opcode 2 at WC idx 0
RDMA RD BW: 240.669283 gbps
MD5: 4c2df7bca511b4d55f2abbbf852a789c
received event: RDMA_CM_EVENT_DISCONNECTED
received expected event: RDMA_CM_EVENT_DISCONNECTED
```
