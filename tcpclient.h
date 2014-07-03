#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include "buffer.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <glib.h>
#include <ev.h>

#define TCPCLIENT_CONNECT_TIMEOUT 2.0
#define TCPCLIENT_RETRY_TIMEOUT 5
#define TCPCLIENT_RECV_BUFFER 65536
#define TCPCLIENT_SEND_QUEUE 134217728	// 128MB

enum tcpclient_event {
	EVENT_CONNECTED,
	EVENT_SENT,
	EVENT_RECV,
	EVENT_ERROR
};

enum tcpclient_state {
    STATE_INIT = 0,
    STATE_CONNECTING,
    STATE_BACKOFF,
    STATE_CONNECTED,
    STATE_TERMINATED
};

static const char *tcpclient_state_name[] = {
	"INIT", "CONNECTING", "BACKOFF", "CONNECTED", "TERMINATED"
};

// data has different meaning depending on the event...
// EVENT_CONNECTED data = NULL
// EVENT_SENT data = pointer to buffer passed to tcpclient_sendall
// EVENT_RECV data = received data (must be free'd manually)
// EVENT_ERROR data = string describing the error
typedef int (*tcpclient_callback)(void *, enum tcpclient_event, char *, size_t);

typedef struct tcpclient_t {
    tcpclient_callback callback_connect;
    tcpclient_callback callback_sent;
    tcpclient_callback callback_recv;
    tcpclient_callback callback_error;

    struct ev_loop *loop;
    ev_timer timeout_watcher;
    ev_io connect_watcher;
    ev_io read_watcher;
    ev_io write_watcher;

	struct addrinfo *addr;
    buffer_t send_queue;
    enum tcpclient_state state;
    time_t last_error;
    int retry_count;
    int sd;
} tcpclient_t;

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
