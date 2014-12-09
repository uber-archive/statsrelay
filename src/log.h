#ifndef STATSRELAY_LOG_H
#define STATSRELAY_LOG_H

// set verbose logging, i.e. send logs to stderr
void stats_log_verbose(int verbose);

// log a message
void stats_log(const char *format, ...);

// finish logging; this ensures that the internally allocated buffer is freed;
// it can safely be called multiple times
void stats_log_end(void);

#endif
