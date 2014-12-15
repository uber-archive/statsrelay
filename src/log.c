#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#define STATSRELAY_LOG_BUF_SIZE 256

static bool g_verbose = 0;
static enum statsrelay_log_level g_level;
static int fmt_buf_size = 0;
static char *fmt_buf = NULL;

void stats_log_verbose(bool verbose) {
	g_verbose = verbose;
}

void stats_set_log_level(enum statsrelay_log_level level) {
	g_level = level;
}

void stats_vlog(const char *prefix,
		const char *format,
		va_list ap) {
	int fmt_len;
	char *np;
	size_t total_written, bw;
	// Allocate the format buffer on the first log call
	if (fmt_buf == NULL) {
		if ((fmt_buf = malloc(STATSRELAY_LOG_BUF_SIZE)) == NULL) {
			goto alloc_failure;
		}
		fmt_buf_size = STATSRELAY_LOG_BUF_SIZE;
	}

	// Keep trying to vsnprintf until we have a sufficiently sized buffer
	// allocated.
	while (1) {
		fmt_len = vsnprintf(fmt_buf, fmt_buf_size, format, ap);

		if (fmt_len < 0) {
			return;  // output error (shouldn't happen for vs* functions)
		} else if (fmt_len < fmt_buf_size) {
			break;  // vsnprintf() didn't truncate
		}

		fmt_buf_size <<= 1;  // double size

		if ((np = realloc(fmt_buf, fmt_buf_size)) == NULL) {
			goto alloc_failure;
		}
		fmt_buf = np;
	}

	if (g_verbose == 1) {
		if (prefix != NULL) {
			fprintf(stderr, prefix);
		}
		total_written = 0;
		while (total_written < fmt_len) {
			// try to write to stderr, but if there are any
			// failures (e.g. parent had closed stderr) then just
			// proceed to the syslog call
			bw = fwrite(fmt_buf + total_written, sizeof(char), fmt_len - total_written, stderr);
			if (bw == 0) {
				break;
			}
			total_written += bw;
		}
		if (total_written >= fmt_len) {
			fputc('\n', stderr);
		}
	}

	syslog(LOG_INFO, fmt_buf, fmt_len);

	if (fmt_buf_size > STATSRELAY_LOG_BUF_SIZE) {
		if ((np = realloc(fmt_buf, STATSRELAY_LOG_BUF_SIZE)) == NULL) {
			goto alloc_failure;
		}
		fmt_buf = np;
		fmt_buf_size = STATSRELAY_LOG_BUF_SIZE;
	}
	return;

alloc_failure:
	stats_log_end();  // reset everything
	return;
}

void stats_debug_log(const char *format, ...) {
	if (g_level <= STATSRELAY_LOG_DEBUG) {
		va_list args;
		va_start(args, format);
		stats_vlog("DEBUG: ", format, args);
		va_end(args);
	}
}

void stats_log(const char *format, ...) {
	if (g_level <= STATSRELAY_LOG_INFO) {
		va_list args;
		va_start(args, format);
		stats_vlog(NULL, format, args);
		va_end(args);
	}
}

void stats_error_log(const char *format, ...) {
	if (g_level <= STATSRELAY_LOG_ERROR) {
		bool orig_verbose = g_verbose;
		g_verbose = true;
		va_list args;
		va_start(args, format);
		stats_vlog("ERROR: ", format, args);
		va_end(args);
		g_verbose = orig_verbose;
	}
}

void stats_log_end(void) {
	free(fmt_buf);
	fmt_buf = NULL;
	fmt_buf_size = 0;
}
