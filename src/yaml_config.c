#include "yaml_config.h"

#include "log.h"

#include <stdbool.h>
#include <string.h>
#include <yaml.h>

// convert a string to a number; stores the result in num, and returns
// true if it really was a number, and false otherwise
static bool convert_number(const char *str, long *num) {
	char *endptr;
	*num = strtol(str, &endptr, 10);
	return endptr != str;
}

static bool set_boolean(const char *strval, bool *bool_val) {
	if (strcmp(strval, "true") == 0) {
		*bool_val = true;
	} else if (strcmp(strval, "false") == 0) {
		*bool_val = false;
	} else {
		stats_error_log("unexpected value \"%s\" for boolean field, "
				"must be true/false", strval);
		return false;
	}
	return true;
}

static void init_proto_config(struct proto_config *protoc) {
	protoc->initialized = false;
	protoc->bind = NULL;
	protoc->enable_validation = true;
	protoc->enable_tcp_cork = true;
	protoc->always_resolve_dns = false;
	protoc->max_send_queue = 134217728;
	protoc->ring = statsrelay_list_new();
}

struct config* parse_config(FILE *input) {
	struct config *config = malloc(sizeof(struct config));
	if (config == NULL) {
		stats_error_log("malloc() error");
		return NULL;
	}

	init_proto_config(&config->carbon_config);
	if (config->carbon_config.ring == NULL) {
		stats_error_log("failed to allocate ring");
		free(config);
		return NULL;
	}
	config->carbon_config.bind = strdup("127.0.0.1:2003");

	init_proto_config(&config->statsd_config);
	config->statsd_config.bind = strdup("127.0.0.1:8125");

	yaml_parser_t parser;
	yaml_event_t event;

	if (!yaml_parser_initialize(&parser)) {
		stats_log("failed to initialize yaml parser");
		goto parse_err;
	}
	yaml_parser_set_input_file(&parser, input);

	struct proto_config *protoc = NULL;
	char *strval;
	long numval;
	int shard_count = -1;
	int map_nesting = 0;
	bool in_document = false;
	bool keep_going = true;
	bool is_key = false;
	bool update_bind = false;
	bool update_send_queue = false;
	bool update_validate = false;
	bool update_tcp_cork = false;
	bool always_resolve_dns = false;
	bool expect_shard_map = false;
	while (keep_going) {
		if (!yaml_parser_parse(&parser, &event)) {
			goto parse_err;
		}

		switch(event.type) {
		case YAML_NO_EVENT:
		case YAML_STREAM_START_EVENT:
			break;  // nothing to do
		case YAML_STREAM_END_EVENT:
			keep_going = false;
			break;
		case YAML_DOCUMENT_START_EVENT:
			if (in_document) {
				stats_error_log("config should not have nested documents");
				goto parse_err;
			}
			in_document = true;
			break;
		case YAML_DOCUMENT_END_EVENT:
			in_document = false;
			break;
		case YAML_SEQUENCE_START_EVENT:
		case YAML_SEQUENCE_END_EVENT:
			stats_error_log("unexpectedly got sequence");
			goto parse_err;
			break;
		case YAML_MAPPING_START_EVENT:
			is_key = true;
			map_nesting++;
			break;
		case YAML_MAPPING_END_EVENT:
			map_nesting--;
			break;
		case YAML_ALIAS_EVENT:
			stats_error_log("don't know how to handle yaml aliases");
			goto parse_err;
			break;
		case YAML_SCALAR_EVENT:
			strval = (char *) event.data.scalar.value;
			switch (map_nesting) {
			case 0:
				stats_error_log("unexpectedly got scalar outside of a map");
				goto parse_err;
				break;
			case 1:
				if (strcmp(strval, "carbon") == 0) {
					protoc = &config->carbon_config;
					config->carbon_config.initialized = true;
				} else if (strcmp(strval, "statsd") == 0) {
					protoc = &config->statsd_config;
					config->statsd_config.initialized = true;
				} else {
					stats_error_log("unexpectedly got map value: \"%s\"", strval);
					goto parse_err;
				}
				break;
			case 2:
				if (is_key) {
					if (strcmp(strval, "bind") == 0) {
						update_bind = true;
					} else if (strcmp(strval, "max_send_queue") == 0) {
						update_send_queue = true;
					} else if (strcmp(strval, "shard_map") == 0) {
						shard_count = -1;
						expect_shard_map = true;
					} else if (strcmp(strval, "validate") == 0) {
						update_validate = true;
					} else if (strcmp(strval, "tcp_cork") == 0) {
						update_tcp_cork = true;
					} else if (strcmp(strval, "always_resolve_dns") == 0) {
						always_resolve_dns = true;
					}
				} else {
					if (update_bind) {
						free(protoc->bind);
						protoc->bind = strdup(strval);
						update_bind = false;
					} else if (update_send_queue) {
						if (!convert_number(strval, &numval)) {
							stats_error_log("max_send_queue was not a number: %s", strval);
						}
						protoc->max_send_queue = numval;
						update_send_queue = false;
					} else if (update_validate) {
						if (!set_boolean(strval, &protoc->enable_validation)) {
							goto parse_err;
						}
						update_validate = false;
					} else if (update_tcp_cork) {
						if (!set_boolean(strval, &protoc->enable_tcp_cork)) {
							goto parse_err;
						}
						update_tcp_cork = false;
					} else if (always_resolve_dns) {
						if (!set_boolean(strval, &protoc->always_resolve_dns)) {
							goto parse_err;
						}
					}
				}
				break;
			case 3:
				if (!expect_shard_map) {
					stats_error_log("was not expecting shard map");
					goto parse_err;
				} else if (is_key) {
					if (!convert_number(strval, &numval)) {
						stats_error_log("shard key was not a number: \"%s\"", strval);
						goto parse_err;

					}
					shard_count++;
					if (numval != shard_count) {
						stats_error_log("expected to see shard key %d, instead saw %d",
							  shard_count, numval);
						goto parse_err;
					}
				} else {
					if (statsrelay_list_expand(protoc->ring) == NULL) {
						stats_error_log("unable to expand list");
						goto parse_err;
					}
					if ((protoc->ring->data[protoc->ring->size - 1]  = strdup(strval)) == NULL) {
						stats_error_log("failed to copy string");
						goto parse_err;
					}
				}
			}
			is_key = !is_key;
			break;
		default:
			stats_error_log("unhandled yaml event");
			goto parse_err;
		}
		yaml_event_delete(&event);
	}

	yaml_parser_delete(&parser);
	return config;

parse_err:
	destroy_config(config);
	yaml_event_delete(&event);
	yaml_parser_delete(&parser);
	return NULL;
}

void destroy_config(struct config *config) {
	if (config != NULL) {
		statsrelay_list_destroy_full(config->carbon_config.ring);
		free(config->carbon_config.bind);
		statsrelay_list_destroy_full(config->statsd_config.ring);
		free(config->statsd_config.bind);
		free(config);
	}
}
