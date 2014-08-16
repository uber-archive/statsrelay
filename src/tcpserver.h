#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ev.h>

typedef struct tcpserver_t tcpserver_t;
typedef struct tcplistener_t tcplistener_t;
typedef struct tcpsession_t tcpsession_t;

tcplistener_t *tcplistener_create(tcpserver_t *server, struct addrinfo *addr, void *(*cb_conn)(int, void *), int (*cb_recv)(int, void *, void *));
void tcplistener_accept_callback(struct ev_loop *loop, struct ev_io *watcher, int revents);
void tcplistener_destroy(tcpserver_t *server, tcplistener_t *listener);

tcpsession_t *tcpsession_create(tcplistener_t *listener);
int tcpsession_get_socket(tcpsession_t *session);
void tcpsession_recv_callback(struct ev_loop *loop, struct ev_io *watcher, int revents);
void tcpsession_destroy(tcpsession_t *session);

tcpserver_t *tcpserver_create(struct ev_loop *loop, void *data);
int tcpserver_bind(tcpserver_t *server, char *address_and_port, char *default_port, void *(*cb_conn)(int, void *), int (*cb_recv)(int, void *, void *));
void tcpserver_destroy(tcpserver_t *server);


#endif
