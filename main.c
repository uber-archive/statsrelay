#include "tcpserver.h"
#include "udpserver.h"
#include "stats.h"
#include "log.h"

#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ev.h>

stats_server_t *server = NULL;
tcpserver_t *ts = NULL;
udpserver_t *us = NULL;
struct ev_loop *loop = NULL;


void graceful_shutdown(struct ev_loop *loop, ev_signal *w, int revents) {
	//stats_log("Received %s (signal %i), shutting down.", strsignal(signum), signum);
	stats_log("Received signal, shutting down.");
	ev_break(loop, EVBREAK_ALL);

	if(ts != NULL) {
		tcpserver_destroy(ts);
	}
	if(us != NULL) {
		udpserver_destroy(us);
	}
	if(server != NULL) {
		stats_server_destroy(server);
	}
}

void reload_config(struct ev_loop *loop, ev_signal *w, int revents) {
	stats_log("Received SIGHUP, reloading.");
	if(server != NULL) {
		stats_server_reload(server);
	}
}

int main(int argc, char **argv) {
	ev_signal *sigint_watcher, *sigterm_watcher, *sighup_watcher;

	if(argc < 2) {
		stats_log("Usage: %s <config file>", argv[0]);
		return 1;
	}

	loop = ev_default_loop(0);

	sigint_watcher = malloc(sizeof(ev_signal));
	ev_signal_init(sigint_watcher, graceful_shutdown, SIGINT);
	ev_signal_start(loop, sigint_watcher);

	sigterm_watcher = malloc(sizeof(ev_signal));
	ev_signal_init(sigterm_watcher, graceful_shutdown, SIGTERM);
	ev_signal_start(loop, sigterm_watcher);

	sighup_watcher = malloc(sizeof(ev_signal));
	ev_signal_init(sighup_watcher, reload_config, SIGHUP);
	ev_signal_start(loop, sighup_watcher);

	server = stats_server_create(argv[1], loop);
	if(server == NULL) {
		free(sigint_watcher);
		free(sigterm_watcher);
		free(sighup_watcher);
		return 2;
	}

	ts = tcpserver_create(loop, server);
	if(ts == NULL) {
		stats_log("Unable to create tcpserver");
		free(sigint_watcher);
		free(sigterm_watcher);
		free(sighup_watcher);
		return 3;
	}

	if(tcpserver_bind(ts, "*", "8125", stats_connection, stats_recv) != 0) {
		free(sigint_watcher);
		free(sigterm_watcher);
		free(sighup_watcher);
		return 4;
	}

	us = udpserver_create(loop, server);
	if(us == NULL) {
		stats_log("Unable to create udpserver");
		free(sigint_watcher);
		free(sigterm_watcher);
		free(sighup_watcher);
		return 5;
	}

	if(udpserver_bind(us, "*", "8125", stats_udp_recv) != 0) {
		free(sigint_watcher);
		free(sigterm_watcher);
		free(sighup_watcher);
		return 6;
	}


	stats_log("main: Starting event loop");
	ev_run(loop, 0);

	free(sigint_watcher);
	free(sigterm_watcher);
	free(sighup_watcher);

	return 0;
}
