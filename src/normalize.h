#ifndef STATSRELAY_NORMALIZE_H
#define STATSRELAY_NORMALIZE_H

#include <stdlib.h>

typedef int (*key_normalizer_t)(const char *, size_t);

int normalize_carbon(char *, size_t);

#endif  // STATSRELAY_NORMALIZE_H
