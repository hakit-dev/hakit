#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "options.h"

int opt_debug = 0;
int opt_daemon = 0;
char *opt_hosts = NULL;

static char *options_command = NULL;
static char *options_parameter_string = NULL;


static void options_help(void)
{
	options_entry_t *entry;
	int tab_len = 0;
	int len;

	fprintf(stderr, "Usage:\n");
	if (options_command) {
		fprintf(stderr, "  %s [OPTION...]", options_command);
		if (options_parameter_string) {
			fprintf(stderr, " %s", options_parameter_string);
		}
		fprintf(stderr, "\n\n");
	}

	if (options_summary) {
		fprintf(stderr, "%s\n\n", options_summary);
	}

	fprintf(stderr,
		"Help Options:\n"
		"  -h, --help\n\n"
		"Application Options:\n");

	entry = (options_entry_t *) options_entries;
	while (entry->long_opt != NULL) {
		len = 2 + strlen(entry->long_opt);
		if (entry->value_symbol) {
			len += 1 + strlen(entry->value_symbol);
		}

		if (len > tab_len) {
			tab_len = len;
		}

		entry++;
	}

	entry = (options_entry_t *) options_entries;
	while (entry->long_opt != NULL) {
		fprintf(stderr, "  ");
		if (entry->short_opt) {
			fprintf(stderr, "-%c, ", entry->short_opt);
		}
		else {
			fprintf(stderr, "    ");
		}
		len = fprintf(stderr, "--%s", entry->long_opt);
		if (entry->value_symbol) {
			len += fprintf(stderr, "=%s", entry->value_symbol);
		}

		while (len < tab_len) {
			fprintf(stderr, " ");
			len++;
		}

		fprintf(stderr, "    %s\n", entry->description);

		entry++;
	}

	fprintf(stderr, "\n");

	exit(1);
}


int options_parse(int *_argc, char *argv[], char *parameter_string)
{
	int argc = *_argc;
	int i, j;

	options_command = strrchr(argv[0], '/');
	if (options_command == NULL) {
		options_command = strrchr(argv[0], '\\');
	}
	if (options_command == NULL) {
		options_command = argv[0];
	}
	else {
		options_command++;
	}

	options_parameter_string = parameter_string;

	i = 1;
	while (i < argc) {
		char *args = argv[i];

		if (args[0] == '-') {
			options_entry_t *found = NULL;
			char *value = NULL;
			int stride = 0;

			options_entry_t *entry = (options_entry_t *) options_entries;
			while ((found == NULL) && (entry->long_opt != NULL)) {
				if (args[1] == '-') {
					char *eq = strchr(args+2, '=');
					char eq_c = '\0';
					if (eq != NULL) {
						eq_c = *eq;
						*eq = '\0';
					}

					if (strcmp(args+2, entry->long_opt) == 0) {
						found = entry;
						if (eq != NULL) {
							value = eq+1;
						}
						stride++;
					}

					if (eq != NULL) {
						*eq = eq_c;
					}
				}
				else {
					if ((args[1] != '\0') && (args[1] == entry->short_opt)) {
						found = entry;
						stride++;

						if (args[2] == '\0') {
							if (entry->value_symbol != NULL) {
								int i1 = i+1;
								if (i1 < argc) {
									value = strdup(argv[i1]);
									stride++;
								}
								else {
									options_help();
									return -1;
								}
							}
						}
						else {
							if (entry->value_symbol != NULL) {
								value = strdup(&args[2]);
							}
							else {
								options_help();
								return -1;
							}
						}
					}
				}

				entry++;
			}

			if (found != NULL) {
				switch (found->type) {
				case OPTIONS_TYPE_INT:
					if (value == NULL) {
						options_help();
						return -1;
					}
					*((int *) found->value_ptr) = strtol(value, NULL, 0);
					break;
				case OPTIONS_TYPE_STRING:
					if (value == NULL) {
						options_help();
						return -1;
					}
					*((char **) found->value_ptr) = value;
					break;
				default:
					*((int *) found->value_ptr) = 1;
					break;
				}

				argc -= stride;
				for (j = i; j <= argc; j++) {
					argv[j] = argv[j+stride];
				}
			}
			else {
				options_help();

				if ((strcmp(args, "--help") == 0) || (strcmp(args, "-h") == 0)) {
					return 0;
				}
				else {
					return -1;
				}
			}
		}
		else {
			break;
		}
	}

	*_argc = argc;

	return 0;
}


void options_usage(void)
{
	options_help();
	exit(1);
}
