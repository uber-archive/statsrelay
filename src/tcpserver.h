#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ev.h>

typedef struct tcpserver_t tcpserver_t;

tcpserver_t *tcpserver_create(struct ev_loop *loop, void *data);
int tcpserver_bind(tcpserver_t *server,
		   const char *address_and_port,
		   void *(*cb_conn)(int, void *),
		   int (*cb_recv)(int, void *, void *));
void tcpserver_destroy(tcpserver_t *server);


#endif
