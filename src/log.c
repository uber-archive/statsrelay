#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include <unistd.h>

int g_verbose = 1;

void stats_log_verbose(int verbose) {
	g_verbose = verbose;
}

void stats_log(const char *format, ...) {
	va_list args;
	char buffer[MAX_LOG_SIZE];

	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	if(g_verbose == 1) {
		va_start(args, format);
		fprintf(stderr, "%i ", getpid());
		vfprintf(stderr, format, args);
		va_end(args);
		fprintf(stderr, "\n");
	}

	va_start(args, format);
	vsyslog(LOG_INFO, format, args);
	va_end(args);
}
