#ifndef STATSRELAY_LOG_H
#define STATSRELAY_LOG_H

#include <stdarg.h>
#include <stdbool.h>

enum statsrelay_log_level {
	STATSRELAY_LOG_DEBUG   = 10,
	STATSRELAY_LOG_INFO    = 20,
	STATSRELAY_LOG_WARN    = 30,
	STATSRELAY_LOG_ERROR   = 40
};

// set verbose logging, i.e. send logs to stderr
void stats_log_verbose(bool verbose);

void stats_set_log_level(enum statsrelay_log_level level);

// variadic log function
void stats_vlog(const char *prefix, const char *format, va_list ap);

// log a message
void stats_log(const char *format, ...);

// log a debug message
void stats_debug_log(const char *format, ...);

// log an error message
void stats_error_log(const char *format, ...);

// finish logging; this ensures that the internally allocated buffer is freed;
// it can safely be called multiple times
void stats_log_end(void);

#endif
