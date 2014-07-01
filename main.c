#include "tcpserver.h"
#include "stats.h"
#include "log.h"

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <ev.h>

tcpserver_t *ts = NULL;

void graceful_shutdown(int signum, siginfo_t *siginfo, void *context) {
	stats_log("Received %s (signal %i), shutting down.", strsignal(signum), signum);
	if(ts != NULL) {
		tcpserver_destroy(ts);
	}
}

int main(int argc, char **argv) {
	stats_server_t *server;
	struct sigaction sa;

	if(argc < 2) {
		stats_log("Usage: %s <config file>", argv[0]);
		return 1;
	}

	sa.sa_sigaction = graceful_shutdown;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);

	if(sigaction(SIGINT, &sa, NULL) != 0) {
		stats_log("Unable to setup SIGINT handler");
		return 1;
	}

	if(sigaction(SIGTERM, &sa, NULL) != 0) {
		stats_log("Unable to setup SIGTERM handler");
		return 1;
	}

	server = stats_server_create(argv[1]);
	if(server == NULL) {
		return 2;
	}

	ts = tcpserver_create(server);
	if(ts == NULL) {
		stats_log("Unable to create tcpserver");
		return 3;
	}

	if(tcpserver_bind(ts, "*", "8125", stats_connection, stats_recv) != 0) {
		return 4;
	}

	stats_log("main: tcpserver running");
	tcpserver_run(ts);

	return 0;
}
