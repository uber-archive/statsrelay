#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "./hashring.h"

static void* my_strdup(const char *str, void *unused_data) {
	return strdup(str);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s /path/to/hash.txt\n", argv[0]);
		return 1;
	}
	hashring_t ring = hashring_init(argv[1], 0, NULL, my_strdup, free);

	if (ring == NULL) {
		fprintf(stderr, "failed to init with config file \"%s\"\n", argv[1]);
		return 1;
	}

	char *line = NULL;
	size_t len;
	ssize_t bytes_read;

	while ((bytes_read = getline(&line, &len, stdin)) != -1) {
		while (bytes_read > 0) {
			if (isspace(line[bytes_read - 1])) {
				line[bytes_read - 1] = '\0';
				bytes_read--;
			} else {
				break;
			}
		}
		printf("\"%s\" %s\n", line, (const char *) hashring_choose(ring, line, bytes_read));
	}
	free(line);
	return 0;
}
