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

#include "version.h"


#define CLASS_NAME "sink"

typedef struct {
	hk_obj_t *obj;
	hk_pad_t *output;
	int id;
	int local;
} ctx_t;


static void _event(ctx_t *ctx, char *name, char *value)
{
	hk_pad_update_str(ctx->output, value);
}


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;

	ctx = malloc(sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	ctx->id = comm_sink_register(obj->name, (comm_sink_func_t) _event, ctx);

	if (hk_prop_get(&obj->props, "local") != NULL) {
		comm_sink_set_local(ctx->id);
	}

	comm_sink_set_widget(ctx->id, hk_prop_get(&obj->props, "widget"));

	return 0;
}

static void _start(hk_obj_t *obj)
{
	ctx_t *ctx = obj->ctx;

	if (ctx->output->value.base != NULL) {
		comm_sink_update_str(ctx->id, ctx->output->value.base);
	}
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
};
