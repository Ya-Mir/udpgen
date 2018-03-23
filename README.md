# udpgen
UDP Packet generator

`git clone https://github.com/Ya-Mir/udpgen/`
`cd udpgen`
`make clean`
`make`
`cd server`
`make clean`
`make`

Либо собирите вручную;
 `gcc -lncurses  client.c -o client`
 `cd server/`
 `gcc -lncurses  server.c -o server`

Сначала запустите server:
`./server`
Потом клиент client
`./client`