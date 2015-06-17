/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <malloc.h>

#include "options.h"
#include "log.h"
#include "str_argv.h"


static void str_shift(char *s)
{
	while (*s != '\0') {
		s[0] = s[1];
		s++;
	}
}


int str_argv(char *line, char ***_argv)
{
	int argc = 0;
	char **argv = NULL;

	while (*line != '\0') {
		/* Skip leading blanks */
		while ((*line != '\0') && (*line <= ' ')) {
			line++;
		}

		char *s2 = line;

		while (*s2 > ' ') {
			if (*s2 == '\\') {
				/* Skip escaped character */
				str_shift(s2);
				if (*s2 != '\0') {
					s2++;
				}
			}
			else if (*s2 == '"') {
				/* Skip leading quote */
				str_shift(s2);

				/* Reach trailing quote */
				while ((*s2 != '\0') && (*s2 != '"')) {
					if (*s2 == '\\') {
						/* Skip escaped character */
						str_shift(s2);
						if (*s2 != '\0') {
							s2++;
						}
					}
					else {
						s2++;
					}
				}

				/* Hide trailing quote */
				if (*s2 == '"') {
					*(s2++) = '\0';
				}
			}
			else {
				/* Normal non-quoted/non-blank character: go ahead */
				s2++;
			}
		}

		/* Mark end of argument */
		if (*s2 != '\0') {
			*(s2++) = '\0';
		}

		/* Push new arg in argument array */
		argv = (char **) realloc(argv, sizeof(char *) * (argc+2));
		argv[argc++] = line;
		argv[argc] = NULL;

		/* Next please... */
		line = s2;
	}

	/* Show argument list for debug */
	if (opt_debug >= 3) {
		int i;

		log_printf("  =>");
		for (i = 0; i < argc; i++) {
			log_printf(" [%d]='%s'", i, argv[i]);
		}
		log_printf("\n");
	}

	*_argv = argv;

	return argc;
}
