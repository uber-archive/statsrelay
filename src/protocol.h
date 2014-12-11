#ifndef STATSRELAY_PROTOCOL_H
#define STATSRELAY_PROTOCOL_H

#include <stdlib.h>

// This header file abstracts the protocol parsing logic. The signature for a
// protocol parser is like:
//
//   size_t parser(const char *instr, const size_t inlen);
//
// The first two paramaters, instr and inlen specify a const pointer to a
// character buffer, and that buffer's size. The function parser will then
// parse this string and find the part of the string that refers to the hash
// key.
//
// Since all of the protocols supported (statsd and carbon) have the key at the
// start of the string, a simplified interace is supported. The return value of
// the parser is the number of bytes that represent the key.
//
// On failure, 0 is returned;

typedef size_t (*protocol_parser_t)(const char *, size_t);

size_t protocol_parser_carbon(const char *, size_t);
size_t protocol_parser_statsd(const char *, size_t);

#endif  // STATSRELAY_PROTOCOL_H
