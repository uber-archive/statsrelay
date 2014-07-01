#include "tcpserver.h"
#include "stats.h"
#include "log.h"

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <ev.h>

stats_server_t *server = NULL;
tcpserver_t *ts = NULL;

void graceful_shutdown(int signum, siginfo_t *siginfo, void *context) {
	stats_log("Received %s (signal %i), shutting down.", strsignal(signum), signum);
	if(ts != NULL) {
		tcpserver_destroy(ts);
	}
}

void reload_config(int signum, siginfo_t *siginfo, void *context) {
	stats_log("Received %s (signal %i), reloading.", strsignal(signum), signum);
	if(server != NULL) {
		stats_server_reload(server);
	}
}

int main(int argc, char **argv) {
	struct sigaction sa_term;
	struct sigaction sa_hup;

	if(argc < 2) {
		stats_log("Usage: %s <config file>", argv[0]);
		return 1;
	}

	sa_term.sa_sigaction = graceful_shutdown;
	sa_term.sa_flags = SA_SIGINFO;
	sigemptyset(&sa_term.sa_mask);

	if(sigaction(SIGINT, &sa_term, NULL) != 0) {
		stats_log("Unable to setup SIGINT handler");
		return 1;
	}

	if(sigaction(SIGTERM, &sa_term, NULL) != 0) {
		stats_log("Unable to setup SIGTERM handler");
		return 1;
	}

	sa_hup.sa_sigaction = reload_config;
	sa_hup.sa_flags = SA_SIGINFO;
	sigemptyset(&sa_term.sa_mask);

	if(sigaction(SIGHUP, &sa_hup, NULL) != 0) {
		stats_log("Unable to setup SIGHUP handler");
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
