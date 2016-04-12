/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2016 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "types.h"
#include "log.h"
#include "options.h"

int opt_debug = 0;
int opt_daemon = 0;

static char *options_command = NULL;


static void options_help(void)
{
	options_entry_t *entry;
	int tab_len = 0;
	int len;

	if (options_summary) {
		fprintf(stderr, "%s\n\n", options_summary);
	}

	fprintf(stderr, "Usage:\n");
	if (options_command) {
		fprintf(stderr, "  %s [OPTION...] APP_FILE\n\n", options_command);
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

	fprintf(stderr, "  -c, ");
	len = fprintf(stderr, "--conf=FILE");
	while (len < tab_len) {
		fprintf(stderr, " ");
		len++;
	}

	fprintf(stderr, "    Set config file (default: " OPTIONS_DEFAULT_CONF ")\n");

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


static char *options_conf_file(int argc, char *argv[])
{
	int i;

	for (i = 1; i < argc; i++) {
		char *args = argv[i];

		if (strcmp(args, "-c") == 0) {
			args += 2;
			if (*args == '\0') {
				return argv[i+1];
			}
			else {
				return args;
			}
		}
		else if (strncmp(args, "--config", 8) == 0) {
			char *eq = strchr(args+2, '=');
			if (eq != NULL) {
				return eq+1;
			}
		}
	}

	return OPTIONS_DEFAULT_CONF;
}


static void options_conf_parse(char *conf_file)
{
	FILE *f = fopen(conf_file, "r");
	if (f != NULL) {
		char buf[1024];
		int lineno = 0;

		while (fgets(buf, sizeof(buf), f) != NULL) {
			lineno++;

			/* Strip leading blanks */
			char *kw = buf;
			while ((*kw != '\0') && (*kw <= ' ')) {
				kw++;
			}

			/* Cut-off comments */
			char *s = strchr(kw, '#');
			if (s != NULL) {
				*s = '\0';
			}

			/* Strip trailing blanks */
			int len = strlen(kw);
			while ((len > 0) && (kw[len-1] <= ' ')) {
				len--;
				kw[len] = '\0';
			}

			/* Ignore empty lines */
			if (*kw == '\0') {
				continue;
			}

			/* Find out config keyword */
			len = 0;
			while ((kw[len] > ' ') && (kw[len] != '=')) {
				len++;
			}

			options_entry_t *entry = (options_entry_t *) options_entries;
			while (entry->long_opt != NULL) {
				if (strncmp(kw, entry->long_opt, len) == 0) {
					break;
				}
				else {
					entry++;
				}
			}

			/* If keyword found, set config value */
			if (entry->long_opt != NULL) {
				char *value = strchr(&kw[len], '=');
				if (value != NULL) {
					value++;

					while ((*value != '\0') && (*value <= ' ')) {
						value++;
					}
				}

				switch (entry->type) {
				case OPTIONS_TYPE_INT:
					if (value == NULL) {
						log_str("WARNING: %s:%d: Missing config value for keyword '%s'", conf_file, lineno, entry->long_opt);
					}
					else {
						*((int *) entry->value_ptr) = strtol(value, NULL, 0);
					}
					break;
				case OPTIONS_TYPE_STRING:
					if (value == NULL) {
						log_str("WARNING: %s:%d: Missing config value for keyword '%s'", conf_file, lineno, entry->long_opt);
					}
					else {
						*((char **) entry->value_ptr) = strdup(value);
					}
					break;
				default:
					if (value == NULL) {
						*((int *) entry->value_ptr) = 1;
					}
					else {
						*((int *) entry->value_ptr) = strtol(value, NULL, 0);
					}
					break;
				}
			}
			else {
				kw[len] = '\0';
				log_str("WARNING: %s:%d: Unkown keyword '%s'", conf_file, lineno, kw);
			}
		}

		fclose(f);
	}
	else {
		log_str("WARNING: Cannot open config file '%s': %s", conf_file, strerror(errno));
	}
}


int options_parse(int *_argc, char *argv[])
{
	int argc = *_argc;
	int i, j;

	/* Get command name */
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

	/* Get config file name */
	char *conf_file = options_conf_file(argc, argv);
	//log_str("Config file: %s", conf_file);

	/* Parse options from config file */
	options_conf_parse(conf_file);

	/* Parse options from command line */
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
									value = argv[i1];
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
								value = &args[2];
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
					*((char **) found->value_ptr) = strdup(value);
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
