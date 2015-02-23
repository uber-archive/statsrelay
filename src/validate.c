#include "validate.h"

#include "log.h"

#include <string.h>

static char *valid_stat_types[6] = {
	"c",
	"ms",
	"kv",
	"g",
	"h",
	"s"
};
static size_t valid_stat_types_len = 6;


int validate_statsd(const char *line, size_t len) {
	size_t plen;
	char c;
	int i, valid;

	// FIXME: this is dumb, don't do a memory copy
	char *line_copy = strndup(line, len);
	char *start, *end;
	char *err;

	start = line_copy;
	plen = len;
	end = memchr(start, ':', plen);
	if (end == NULL) {
		stats_log("validate: Invalid line \"%.*s\" missing ':'", len, line);
		goto statsd_err;
	}

	if ((end - start) < 1) {
		stats_log("validate: Invalid line \"%.*s\" zero length key", len, line);
		goto statsd_err;
	}

	start = end + 1;
	plen = len - (start - line_copy);

	c = end[0];
	end[0] = '\0';
	if ((strtod(start, &err) == 0.0) && (err == start)) {
		stats_log("validate: Invalid line \"%.*s\" unable to parse value as double", len, line);
		goto statsd_err;
	}
	end[0] = c;

	end = memchr(start, '|', plen);
	if (end == NULL) {
		stats_log("validate: Invalid line \"%.*s\" missing '|'", len, line);
		goto statsd_err;
	}

	start = end + 1;
	plen = len - (start - line_copy);

	end = memchr(start, '|', plen);
	if (end != NULL) {
		c = end[0];
		end[0] = '\0';
		plen = end - start;
	}

	valid = 0;
	for (i = 0; i < valid_stat_types_len; i++) {
		if (strlen(valid_stat_types[i]) != plen) {
			continue;
		}
		if (strncmp(start, valid_stat_types[i], plen) == 0) {
			valid = 1;
			break;
		}
	}

	if (valid == 0) {
		stats_log("validate: Invalid line \"%.*s\" unknown stat type \"%.*s\"", len, line, plen, start);
		goto statsd_err;
	}

	if (end != NULL) {
		end[0] = c;
		// end[0] is currently the second | char
		// test if we have at least 1 char following it (@)
		if ((len - (end - line_copy) > 1) && (end[1] == '@')) {
			start = end + 2;
			plen = len - (start - line_copy);
			if (plen == 0) {
				stats_log("validate: Invalid line \"%.*s\" @ sample with no rate", len, line);
				goto statsd_err;
			}
			if ((strtod(start, &err) == 0.0) && err == start) {
				stats_log("validate: Invalid line \"%.*s\" invalid sample rate", len, line);
				goto statsd_err;
			}
		} else {
			stats_log("validate: Invalid line \"%.*s\" no @ sample rate specifier", len, line);
			goto statsd_err;
		}
	}

	free(line_copy);
	return 0;

statsd_err:
	free(line_copy);
	return 1;
}

int validate_carbon(const char *line, size_t len) {
	int spaces_found = 0;
	const char *p = line;
	size_t n = len;
	while (1) {
		const char *s = memchr(p, ' ', n);
		if (s == NULL) {
			break;
		}
		spaces_found++;
		n = len - (s - line) - 1;
		p = s + 1;
		if (spaces_found > 2) {
			break;
		}
	}
	if (spaces_found != 2) {
		stats_log("validate: found %d spaces in invalid carbon line", spaces_found);
		return 1;
	}
	if ((strncmp("carbon.", line, 7) != 0) &&
	    (strncmp("servers.", line, 8) != 0) &&
	    (strncmp("stats.", line, 6) != 0)) {
		stats_log("validate: Invalid line \"%.*s\" does not start with (carbon|servers|stats).", len, line);
		return 1;
	}
	return 0;
}
