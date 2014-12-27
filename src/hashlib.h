#ifndef STATSRELAY_HASHLIB_H
#define STATSRELAY_HASHLIB_H

#include <stdint.h>

// hash a key to get a value in the range [0, output_domain)
uint32_t stats_hash(const char *key,
		    uint32_t keylen,
		    uint32_t output_domain);

#endif  // STATSRELAY_HASHLIB_H
