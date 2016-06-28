/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "log.h"
#include "mod.h"

#include "version.h"


#define CLASS_NAME "not"


typedef struct {
	hk_obj_t *obj;
	hk_pad_t *input;
	hk_pad_t *output;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;

	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->input = hk_pad_create(obj, HK_PAD_IN, "in");
	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

	pad->state = atoi(value) ? 0:1;
	hk_pad_update_int(ctx->output, pad->state);
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
