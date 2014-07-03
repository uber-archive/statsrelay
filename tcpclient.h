#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include <ev.h>

#define TCPCLIENT_CONNECT_TIMEOUT 2.0
#define TCPCLIENT_RETRY_TIMEOUT 5
#define TCPCLIENT_RECV_BUFFER 65537

typedef struct tcpclient_t tcpclient_t;

enum tcpclient_event {
	EVENT_CONNECTED,
	EVENT_SENT,
	EVENT_RECV,
	EVENT_ERROR
};

// data has different meaning depending on the event...
// EVENT_CONNECTED data = NULL
// EVENT_SENT data = pointer to buffer passed to tcpclient_sendall
// EVENT_RECV data = received data (must be free'd manually)
// EVENT_ERROR data = string describing the error
typedef int (*tcpclient_callback)(tcpclient_t *, enum tcpclient_event, char *data, size_t len);

int tcpclient_init(tcpclient_t *client,
		struct ev_loop *loop);

void tcpclient_set_connect_callback(tcpclient_t *client,
		tcpclient_callback *callback);
void tcpclient_set_sent_callback(tcpclient_t *client,
		tcpclient_callback *callback);
void tcpclient_set_recv_callback(tcpclient_t *client,
		tcpclient_callback *callback);
void tcpclient_set_error_callback(tcpclient_t *client,
		tcpclient_callback *callback);

int tcpclient_connect(tcpclient_t *client,
		char *host,
		char *port);

int tcpclient_sendall(tcpclient_t *client,
		char *buf,
		size_t len);

void tcpclient_destroy(tcpclient_t *client,
		int drop_queued);

#endif
