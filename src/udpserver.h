#ifndef UDPSERVER_H
#define UDPSERVER_H

#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ev.h>

typedef struct udpserver_t udpserver_t;

udpserver_t *udpserver_create(struct ev_loop *loop, void *data);
int udpserver_bind(udpserver_t *server,
		   const char *address_and_port,
		   int (*cb_recv)(int, void *));
void udpserver_destroy(udpserver_t *server);

#endif
