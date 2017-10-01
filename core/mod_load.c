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
#include <malloc.h>

#include "log.h"
#include "buf.h"
#include "str_argv.h"
#include "mod.h"
#include "mod_load.h"

typedef enum {
	SECTION_OBJECTS=0,
	SECTION_NETS,
	SECTION_UNKNOWN,
	NSECTIONS
} load_section_t;

typedef struct {
	char *fname;
	int lnum;
	load_section_t section;
} load_ctx_t;


static inline int strempty(char *str)
{
	return (str == NULL) || (*str == '\0');
}


static int hk_mod_load_object(load_ctx_t *ctx, char *name, int argc, char **argv)
{
	hk_class_t *class;
	hk_obj_t *obj;

	/* Check object name is provided */
	if (strempty(name)) {
		log_str("ERROR: %s:%d: Missing object name", ctx->fname, ctx->lnum);
		return -1;
	}

	/* Check class name is provided */
	if (argc < 1) {
		log_str("ERROR: %s:%d: Missing class name", ctx->fname, ctx->lnum);
		return -1;
	}

	/* Retrieve class */
	class = hk_class_find(argv[0]);
	if (class == NULL) {
		log_str("ERROR: %s:%d: Unknown class '%s'", ctx->fname, ctx->lnum, argv[0]);
		return -1;
	}

	/* Create object */
	obj = hk_obj_create(class, name, argc-1, argv+1);
	if (obj == NULL) {
		log_str("PANIC: %s:%d: Failed to create object '%s'", ctx->fname, ctx->lnum, name);
		return -1;
	}

	if (class->new != NULL) {
		if (class->new(obj) < 0) {
			log_str("ERROR: %s:%d: Failed to setup object '%s'", ctx->fname, ctx->lnum, name);
			return -1;
		}
	}

	return 0;
}


static int hk_mod_load_net(load_ctx_t *ctx, int argc, char **argv)
{
	hk_net_t *net;
	int i;

	/* Create net */
	net = hk_net_create();
	if (net == NULL) {
		log_str("PANIC: %s:%d: Failed to create new net", ctx->fname, ctx->lnum);
		return -1;
	}

	/* Attach pads to this net */
	for (i = 0; i < argc; i++) {
		char *args = argv[i];
		char *pt = strchr(args, '.');
		hk_obj_t *obj;
		hk_pad_t *pad;

		if (pt == NULL) {
			log_str("ERROR: %s:%d: Syntax error in pad specification '%s'", ctx->fname, ctx->lnum, args);
			return -1;
		}

		*pt = '\0';
		obj = hk_obj_find(args);
		*pt = '.';

		if (obj == NULL) {
			log_str("ERROR: %s:%d: Referencing undefined object '%s'", ctx->fname, ctx->lnum, args);
			return -1;
		}

		pad = hk_pad_find(obj, pt+1);
		if (pad == NULL) {
			log_str("ERROR: %s:%d: Referencing unknown pad '%s' in object '%s'", ctx->fname, ctx->lnum, pt+1, obj->name);
			return -1;
		}

		hk_net_connect(net, pad);
	}

	return 0;
}


static int hk_mod_load_line(load_ctx_t *ctx, char *line)
{
	int ret = 0;
	char *name = NULL;
	int argc = 0;
	char **argv = NULL;
	char *s;

	log_debug(2, "hk_mod_load_line %s:%d: '%s'", ctx->fname, ctx->lnum, line);

	/* Skip leadink blanks */
	while ((*line <= ' ') && (*line != '\0')) {
		line++;
	}

	/* Ignore empty lines */
	if (*line == '\0') {
		return 0;
	}

	/* Ignore commented lines */
	if ((*line == ';') || (*line == '#')) {
		return 0;
	}

	/* Check for new section id */
	if (*line == '[') {
		if (strcmp(line, "[objects]") == 0) {
			ctx->section = SECTION_OBJECTS;
		}
		else if (strcmp(line, "[nets]") == 0) {
			ctx->section = SECTION_NETS;
		}
		else {
			ctx->section = SECTION_UNKNOWN;
			log_str("WARNING: %s:%s: ignoring unknown section %s", ctx->fname, ctx->lnum, line);
		}

		return 0;
	}

	/* Try to extract element name */
	s = line;
	while ((*s > ' ') && (*s != ':')) {
		s++;
	}

	if (*s == ':') {
		name = line;
		*(s++) = '\0';
		log_debug(3, "  name='%s'", name);
	}
	else {
		s = line;
		log_debug(3, "  name=(none)");
	}

	/* Extract element arguments */
	argc = str_argv(s, &argv);

	switch (ctx->section) {
	case SECTION_OBJECTS:
		ret = hk_mod_load_object(ctx, name, argc, argv);
		break;
	case SECTION_NETS:
		ret = hk_mod_load_net(ctx, argc, argv);
		break;
	default:
		break;
	}

	if (argv != NULL) {
		free(argv);
	}

	return ret;
}


int hk_mod_load(char *fname)
{
	int ret = 0;
	FILE *f;
	buf_t buf;
	load_ctx_t ctx = {
		.fname = fname,
		.section = SECTION_OBJECTS,
	};

	log_debug(2, "hk_mod_load '%s'", fname);

	f = fopen(fname, "r");
	if (f == NULL) {
		log_str("ERROR: Cannot open file '%s': %s", fname, strerror(errno));
		return -1;
	}

	buf_init(&buf);

	int check_nl = 0;
	int lnum = 0;

	while ((ret == 0) && (!feof(f))) {
		char str[1024];
		int len = fread(str, 1, sizeof(str)-1, f);
		if (len > 0) {
			int i;

			// Replace all non-printable characters with spaces
			for (i = 0; i < len; i++) {
				if ((str[i] < ' ') && (str[i] != '\n')) {
					str[i] = ' ';
				}
			}

			// Put NUL character at end of buffer to ease string parsing
			str[len] = '\0';

			char *s1 = str;

			// If previous buffer chunk ended with \n, perform line continuation check now
			if (check_nl) {
				check_nl = 0;
				if (*s1 != ' ') {
					ret = hk_mod_load_line(&ctx, (char *) buf.base);
					buf.len = 0;
				}
			}

			while (s1 != NULL) {
				char *s2 = strchr(s1, '\n');
				if (s2 != NULL) {
					lnum++;
					if (buf.len == 0) {
						ctx.lnum = lnum;
					}

					*(s2++) = '\0';
					buf_append_str(&buf, s1);

					if (*s2 == '\0') {
						// We don't have enough characters to check whether the next lines begins with a space.
						// So we need to deffer this processing to the next buffer chunk
						check_nl = 1;
						s1 = NULL;
					}
					else if (*s2 != ' ') {
						// If the next line does not begins with a space,
						// the current line is complete and must be processed
						ret = hk_mod_load_line(&ctx, (char *) buf.base);
						buf.len = 0;
					}
				}
				else {
					buf_append_str(&buf, s1);
				}

				s1 = s2;
			}
		}
		else if (len < 0) {
			log_str("ERROR: Cannot read file '%s': %s", fname, strerror(errno));
			ret = -1;
		}
	}

	if (ret == 0) {
		if (buf.len > 0) {
			buf.base[buf.len] = '\0';
			ret = hk_mod_load_line(&ctx, (char *) buf.base);
		}
	}

	buf_cleanup(&buf);

	fclose(f);

	return ret;
}
