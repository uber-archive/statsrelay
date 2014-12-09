#include "protocol.h"

#include <string.h>

static size_t simple_parse(const char *instr, size_t inlen, const char needle) {
	if (instr == 0 || inlen == 0) {
		return 0;
	}
	const char *p = memchr(instr, needle, inlen);
	if (p == NULL) {
		return 0;
	}
	return p - instr;
}

size_t protocol_parser_carbon(const char *instr, size_t inlen) {
	return simple_parse(instr, inlen, ' ');
}

size_t protocol_parser_statsd(const char *instr, size_t inlen) {
	return simple_parse(instr, inlen, ':');
}
