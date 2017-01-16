// This module handles all outbound network communication. Despite it's name,
// it is also capable of connecting to UDP endpoints.

#ifndef STATSRELAY_TCPCLIENT_H
#define STATSRELAY_TCPCLIENT_H

#include "config.h"
#include "buffer.h"
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <ev.h>

#include "yaml_config.h"

#define TCPCLIENT_CONNECT_TIMEOUT 2.0
#define TCPCLIENT_RETRY_TIMEOUT 1
#define TCPCLIENT_RECV_BUFFER 65536
#define TCPCLIENT_SEND_QUEUE 134217728	// 128MB
#define TCPCLIENT_NAME_LEN 256

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

// data has different meaning depending on the event...
// EVENT_CONNECTED data = NULL
// EVENT_SENT data = pointer to buffer passed to tcpclient_sendall
// EVENT_RECV data = received data (must be free'd manually)
// EVENT_ERROR data = string describing the error
typedef int (*tcpclient_callback)(void *, enum tcpclient_event, void *, char *, size_t);

typedef struct io_watcher_t {
	ev_io watcher;
	bool started;
} io_watcher_t;

typedef struct tcpclient_t {
	tcpclient_callback callback_connect;
	tcpclient_callback callback_sent;
	tcpclient_callback callback_recv;
	tcpclient_callback callback_error;
	void *callback_context;

	struct ev_loop *loop;
	ev_timer timeout_watcher;
	io_watcher_t connect_watcher;
	io_watcher_t read_watcher;
	io_watcher_t write_watcher;

	char name[TCPCLIENT_NAME_LEN];
	struct addrinfo *addr;
	buffer_t send_queue;
	enum tcpclient_state state;
	time_t last_error;
	int retry_count;
	int failing;
	int sd;
	int socktype;

	char *host;
	char *port;
	char *protocol;

	struct proto_config *config;
} tcpclient_t;

int tcpclient_init(tcpclient_t *client,
		   struct ev_loop *loop,
		   void *callback_connect,
		   struct proto_config *config,
		   char *host,
		   char *port,
		   char *protocol);

void tcpclient_set_sent_callback(tcpclient_t *client,
				 tcpclient_callback callback);

int tcpclient_connect(tcpclient_t *client);

int tcpclient_sendall(tcpclient_t *client,
		      const char *buf,
		      size_t len);

void tcpclient_destroy(tcpclient_t *client,
		       int drop_queued);

#endif  // STATSRELAY_TCPCLIENT_H
