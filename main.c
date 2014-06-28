#include "tcpserver.h"
#include "stats.h"
#include "list.h"
#include "log.h"

#include "ketama.h"

#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <ev.h>


void graceful_shutdown(int signum, siginfo_t *siginfo, void *context) {
	stats_log("Received %s (signal %i), shutting down.", strsignal(signum), signum);
}

int main(int argc, char **argv) {
	ketama_continuum *kc;
	tcpserver_t *ts;
	int option_index = 0;
	int c;
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

	if(ketama_roll(kc, argv[1]) == 0) {
		stats_log(ketama_error());
		stats_log("Unable to load ketama config from %s", argv[1]);
		return 2;
	}
	//ketama_print_continuum(*kc);

	ts = tcpserver_create((void *)kc);
	if(ts == NULL) {
		stats_log("Unable to create tcpserver");
		return 3;
	}

	if(tcpserver_bind(ts, "*", "8125", stats_connection, stats_recv) != 0) {
		return 4;
	}

	stats_log("main: tcpserver running");
	tcpserver_run((void *)kc);

	return 0;
}
