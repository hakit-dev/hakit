/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "log.h"
#include "buf.h"
#include "mod.h"
#include "mod_load.h"

typedef enum {
	SECTION_NONE=0,
	SECTION_OBJECTS,
	SECTION_NETS,
	NSECTIONS
} load_section_t;

typedef struct {
	char *fname;
	int lnum;
	load_section_t section;
} load_ctx_t;


static void hk_mod_load_line(load_ctx_t *ctx, char *line)
{
	char *name = NULL;
	char *s;

	log_debug(2, "hk_mod_load_line (%d) '%s'", ctx->lnum, line);

	/* Check for new section id */
	if (*line == '[') {
		if (strcmp(line, "[objects]") == 0) {
			ctx->section = SECTION_OBJECTS;
		}
		else if (strcmp(line, "[nets]") == 0) {
			ctx->section = SECTION_NETS;
		}
		else {
			log_str("ERROR: %s:%s: unknown section definition '%s'", ctx->fname, ctx->lnum, line);
		}

		return;
	}

	/* Skip leadink blanks */
	while ((*line <= ' ') && (*line != '\0')) {
		line++;
	}

	if (*line == '\0') {
		return;
	}

	/* Try to extract element name */
	s = line;
	while ((*s > ' ') && (*s != ':')) {
		s++;
	}

	if (*s == ':') {
		name = line;
		*(s++) = '\0';
		log_debug(2, "  name='%s'", name);
	}

	switch (ctx->section) {
	case SECTION_OBJECTS:
		break;
	case SECTION_NETS:
		break;
	default:
		break;
	}
}


int hk_mod_load(char *fname)
{
	FILE *f;
	buf_t buf;
	load_ctx_t ctx = {
		.fname = fname,
		.section = SECTION_NONE,
	};

	log_debug(2, "hk_mod_load '%s'", fname);

	f = fopen(fname, "r");
	if (f == NULL) {
		log_str("ERROR: Cannot open file '%s': %s", fname, strerror(errno));
		return -1;
	}

	buf_init(&buf);
	buf_grow(&buf, 1);

	while (!feof(f)) {
		char *str = (char *) buf.base + buf.len;

		if (fgets(str, buf.size-buf.len, f) != NULL) {
			buf.len += strlen(str);

			if ((buf.len > 0) && (buf.base[buf.len-1] == '\n')) {
				ctx.lnum++;

				buf.len--;
				buf.base[buf.len] = '\0';

				if (buf.len > 0) {
					if (buf.base[buf.len-1] == '\\') {
						buf.len--;
						buf.base[buf.len] = '\0';
					}
					else {
						hk_mod_load_line(&ctx, (char *) buf.base);
						buf.len = 0;
					}
				}
			}
			else {
				buf_grow(&buf, 1);
			}
		}
	}

	if (buf.len > 0) {
		buf.base[buf.len] = '\0';
		hk_mod_load_line(&ctx, (char *) buf.base);
	}

	buf_cleanup(&buf);

	fclose(f);

	return 0;
}
