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
#include <glob.h>

#include "types.h"
#include "log.h"
#include "options.h"

int opt_debug = 0;
int opt_daemon = 0;

static char *conf_dir = NULL;


static void options_help(const options_entry_t *entries, char *command)
{
	options_entry_t *entry;
	int tab_len = 0;
	int len;

	if (options_summary) {
		fprintf(stderr, "%s\n\n", options_summary);
	}

	fprintf(stderr, "Usage:\n");
	if (command != NULL) {
		fprintf(stderr, "  %s [OPTION...] TILE...\n\n", command);
	}

	fprintf(stderr,
		"Help Options:\n"
		"  -h, --help\n\n"
		"Application Options:\n");

	entry = (options_entry_t *) entries;
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
	len = fprintf(stderr, "--config=DIR");
	while (len < tab_len) {
		fprintf(stderr, " ");
		len++;
	}

	fprintf(stderr, "    Set config directory (default: " OPTIONS_DEFAULT_CONF_DIR ")\n");

	entry = (options_entry_t *) entries;
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


static void options_set_conf_dir(int argc, char *argv[])
{
	char *dir = NULL;
	int i;

        /* Search for option --config= */
	for (i = 1; (i < argc) && (dir == NULL); i++) {
		char *args = argv[i];

		if (strcmp(args, "-c") == 0) {
			args += 2;
			if (*args == '\0') {
				dir = argv[i+1];
			}
			else {
				dir = args;
			}
		}
		else if (strncmp(args, "--config", 8) == 0) {
			char *eq = strchr(args+2, '=');
			if (eq != NULL) {
				dir = eq+1;
			}
		}
	}

	if (dir != NULL) {
		int len = strlen(dir);

		/* Strip leading dir separator(s) */
		while ((len > 0) && (dir[len-1] == '/')) {
			len--;
			dir[len] = '\0';
		}

		if (conf_dir != NULL) {
			free(conf_dir);
		}
		conf_dir = strdup(dir);
	}
}


static int options_parse_value(const options_entry_t *entry, char *value)
{
	switch (entry->type) {
	case OPTIONS_TYPE_INT:
		if (value == NULL) {
			return -1;
		}
		else {
			*((int *) entry->value_ptr) = strtol(value, NULL, 0);
		}
		break;
	case OPTIONS_TYPE_STRING:
		if (value == NULL) {
			return -1;
		}
		else {
                        char **ptr = (char **) entry->value_ptr;
                        if (entry->flags & OPTION_FLAG_LIST) {
                                int len1 = 0;
                                if (*ptr != NULL) {
                                        len1 = strlen(*ptr);
                                }
                                int len = len1 + 1 + strlen(value);
                                *ptr = realloc(*ptr, len+1);
                                char *str = *ptr;
                                if (len1 > 0) {
                                        str[len1++] = ',';
                                }
                                strcpy(&str[len1], value);
                        }
                        else {
                                if (*ptr != NULL) {
                                        free(*ptr);
                                }
                                *ptr = strdup(value);
                        }
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

	return 0;
}


static int options_conf_parse_path(const options_entry_t *entries, char *path)
{
	char buf[1024];
	int lineno = 0;
	int ret = 0;

	/* Open config file from config directory */
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		log_str("ERROR: Cannot open file '%s': %s", path, strerror(errno));
		return -1;
	}

        log_str("Parsing option file '%s'", path);

	while ((ret == 0) && (fgets(buf, sizeof(buf), f) != NULL)) {
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

		options_entry_t *entry = (options_entry_t *) entries;
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

			if (options_parse_value(entry, value)) {
				log_str("ERROR: %s:%d: Missing config value for keyword '%s'", path, lineno, entry->long_opt);
				ret = -2;
			}
		}
		else {
			kw[len] = '\0';
			log_str("WARNING: %s:%d: Unknown keyword '%s'", path, lineno, kw);
		}
	}

	fclose(f);

	return ret;
}


static char *option_conf_path(char *conf_file)
{
	char *dir = (conf_dir != NULL) ? conf_dir : OPTIONS_DEFAULT_CONF_DIR;
        size_t size = strlen(dir) + strlen(conf_file) + 2;
	char *path = malloc(size);
        if (path != NULL) {
                snprintf(path, size, "%s/%s", dir, conf_file);
        }
        return path;
}


int options_conf_parse(const options_entry_t *entries, char *conf_file)
{
        int ret = -1;

        char *path = option_conf_path(conf_file);
        if (path != NULL) {
                ret = options_conf_parse_path(entries, path);
                free(path);
        }

        return ret;
}


int options_parse(const options_entry_t *entries, char *conf_file, int *_argc, char *argv[])
{
	int argc = *_argc;
	char *command;
	int i, j;

	/* Get command name */
	command = strrchr(argv[0], '/');
	if (command == NULL) {
		command = strrchr(argv[0], '\\');
	}
	if (command == NULL) {
		command = argv[0];
	}
	else {
		command++;
	}

	/* Get config file name */
	options_set_conf_dir(argc, argv);

	/* Parse options from config file */
        if (conf_file != NULL) {
                char *path = option_conf_path(conf_file);
                if (path != NULL) {
                        glob_t globbuf = {};

                        if (glob(path, 0, NULL, &globbuf) == 0) {
                                for (i = 0; i < globbuf.gl_pathc; i++) {
                                        char *str = globbuf.gl_pathv[i];
                                        int len = strlen(str);
                                        if (len > 0) {
                                                if (str[len-1] == '~') {
                                                        continue;
                                                }
                                        }
                                        //fprintf(stderr, "[%d] = '%s'\n", i, str);
                                        options_conf_parse_path(entries, str);
                                }
                        }

                        globfree(&globbuf);
                        free(path);
                }
        }

	/* Parse options from command line */
	i = 1;
	while (i < argc) {
		char *args = argv[i];

		if (args[0] == '-') {
			options_entry_t *found = NULL;
			char *value = NULL;
			int stride = 0;

			options_entry_t *entry = (options_entry_t *) entries;
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
									options_help(entries, command);
									return -1;
								}
							}
						}
						else {
							if (entry->value_symbol != NULL) {
								value = &args[2];
							}
							else {
								options_help(entries, command);
								return -1;
							}
						}
					}
				}

				entry++;
			}

			if (found != NULL) {
				if (options_parse_value(found, value)) {
					options_help(entries, command);
					return -1;
				}

				argc -= stride;
				for (j = i; j <= argc; j++) {
					argv[j] = argv[j+stride];
				}
			}
			else {
				options_help(entries, command);

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
