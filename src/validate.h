#ifndef STATSRELAY_VALIDATE_H
#define STATSRELAY_VALIDATE_H

#include <stdlib.h>

typedef int (*validate_line_validator_t)(const char *, size_t);

int validate_statsd(const char *, size_t);
int validate_carbon(const char *, size_t);

#endif  // STATSRELAY_VALIDATE_H
