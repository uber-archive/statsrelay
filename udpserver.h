#ifndef UDPSERVER_H
#define UDPSERVER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ev.h>

typedef struct udpserver_t udpserver_t;
typedef struct udplistener_t udplistener_t;

udplistener_t *udplistener_create(udpserver_t *server, struct addrinfo *addr, int (*cb_recv)(int, void *));
void udplistener_destroy(udpserver_t *server, udplistener_t *listener);

udpserver_t *udpserver_create(struct ev_loop *loop, void *data);
int udpserver_bind(udpserver_t *server, char *address_and_port, char *default_port, int (*cb_recv)(int, void *));
void udpserver_destroy(udpserver_t *server);

#endif
