#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>

static struct option long_options[] = {
	{"port", required_argument, NULL, 'p'},
	{"stat-names", required_argument, NULL, 's'},
	{"help", no_argument, NULL, 'h'},
};

static void print_help(const char *progname) {
	printf("help\n");
}


int main(int argc, char **argv) {
	uint16_t port = 0;
	const char *stat_names = NULL;
	int option_index = 0;
	int8_t c;
	while ((c = (int8_t)getopt_long(argc, argv, "p:s:h", long_options, &option_index)) != -1) {
		switch (c) {
		case 0:
		case 'h':
			print_help(argv[0]);
			return 1;
		case 'p':
			port = (uint16_t) strtol(optarg, NULL, 10);
			break;
		case 's':
			stat_names = strdup(optarg);
			if (stat_names == NULL) {
				perror("failed to strdup()");
				goto err;
			}
			break;
		default:
			fprintf(stderr, "%s: Unknown argument %c", argv[0], c);
			goto err;
		}
	}
	if (stat_names == NULL) {
		fprintf(stderr, "missing -s option\n");
		goto err;
	} else if (port == 0) {
		fprintf(stderr, "missing or invalid -p option\n");
		goto err;
	}

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		perror("failed to socket()");
		goto err;
	}
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	printf("connecting to port %hu\n", port);
	if (connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		perror("failed to connect()");
		goto err;
	}

	FILE *fp = fopen(stat_names, "r");
	if (fp == NULL) {
		goto err;
	}


	struct timeval t0, t1, total;
	if (gettimeofday(&t0, NULL) == -1) {
		perror("gettimeofday()");
		goto err;
	}
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	size_t lines_sent = 0;

	while ((read = getline(&line, &len, fp)) != -1) {
		bool need_realloc = false;
		while (read + 6 > len) {
			need_realloc = true;
			len <<= 1;
		}
		if (need_realloc) {
			void *np = realloc(line, len);
			if (np == NULL) {
				perror("failed to realloc()");
				goto err;
			}
			line = np;
		}
		while (line[read] == '\n' || line[read] == '\0') {
			read--;
		}
		memcpy(line + read + 1, ":1|c\n\0", 6);
		size_t goal = strlen(line);
		size_t total_sent = 0;
		while (total_sent < goal) {
			ssize_t bytes_sent = send(
				sock, line + total_sent, goal - total_sent, 0);
			if (bytes_sent <= 0) {
				perror("failed to send()");
				goto err;
			}
			total_sent += bytes_sent;
		}
		lines_sent++;
	}

	if (lines_sent == 0) {
		goto err;
	} else if (gettimeofday(&t1, NULL) == -1) {
		perror("gettimeofday()");
		goto err;
	}

	timersub(&t1, &t0, &total);
	double total_micros = total.tv_sec * 1000000 + total.tv_usec;
	printf("sent %zd lines in %lu microseconds = %6.3f microseconds per line\n",
	       lines_sent,
	       (unsigned long) total_micros,
	       total_micros / lines_sent);

	return 0;

err:
	return 1;
}
