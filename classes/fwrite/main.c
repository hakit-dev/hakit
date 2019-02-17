/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2016 Sylvain Giroudon
 *
 * File write
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>

#include "log.h"
#include "env.h"
#include "mod.h"

#include "version.h"


#define CLASS_NAME "fwrite"

typedef struct {
	hk_obj_t *obj;
        char *path;
	hk_pad_t *pad_in;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *str;

	/* Get file path */
	str = hk_prop_get(&obj->props, "path");
	if (str == NULL) {
		log_str("ERROR: %s: No file path defined", obj->name);
		return -1;
	}

	/* Create object context */
	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

        /* Set file path */
        ctx->path = strdup(str);

	/* Create input/output pads */
	ctx->pad_in = hk_pad_create(obj, HK_PAD_IN, "in");

	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

	if (pad == ctx->pad_in) {
                FILE *f = fopen(ctx->path, "w");
                if (f != NULL) {
                        fwrite(value, 1, strlen(value), f);
                        fclose(f);
                }
                else {
                        log_str("ERROR: %s: Cannot open file '%s': %s", ctx->obj->name, ctx->path, strerror(errno));
                }
	}
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
