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
#include <malloc.h>

#include "log.h"
#include "mod.h"
#include "comm.h"


#define CLASS_NAME "sink"

typedef struct {
	hk_obj_t *obj;
	hk_pad_t *output;
	int inv;
} ctx_t;


static void _event(ctx_t *ctx, char *name, char *value)
{
	hk_pad_update_str(ctx->output, value);
}


static hk_obj_t *_new(hk_obj_t *obj)
{
	ctx_t *ctx;

	ctx = malloc(sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");
	ctx->inv = hk_prop_get_int(&obj->props, "inv");

	comm_sink_register(obj->name, (comm_sink_func_t) _event, ctx);

	return obj;
}


hk_class_t hk_class_and = {
	.name = CLASS_NAME,
	.new = _new,
};
