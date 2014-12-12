#ifndef STATSRELAY_LOG_H
#define STATSRELAY_LOG_H

#include <stdarg.h>
#include <stdbool.h>

// set verbose logging, i.e. send logs to stderr
void stats_log_verbose(int verbose);

// variadic log function
void stats_vlog(bool debug, const char *format, va_list ap);

// log a message
void stats_log(const char *format, ...);

// log a debug message
void stats_debug_log(const char *format, ...);

// finish logging; this ensures that the internally allocated buffer is freed;
// it can safely be called multiple times
void stats_log_end(void);

#endif
