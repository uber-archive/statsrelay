#include <stdio.h>
#include <stdint.h>
#include <ev.h>

#include "../stats.h"
#include "../tcpclient.h"


int main(int argc, char **argv) {
	stats_server_t *server;
	struct ev_loop *loop = ev_default_loop(0);

	server = stats_server_create("tests/statsrelay_tcp.conf", loop);
	if(server == NULL) {
		return 1;
	}

	stats_server_destroy(server);
	return 0;
}
