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


#define CLASS_NAME "source"

typedef struct {
	hk_obj_t *obj;
	hk_pad_t *input;
	int inv;
	int source;
} ctx_t;


static hk_obj_t *_new(hk_obj_t *obj)
{
	ctx_t *ctx;
	int event;

	ctx = malloc(sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->input = hk_pad_create(obj, HK_PAD_IN, "in");
	ctx->inv = hk_prop_get_int(&obj->props, "inv");
	event = (hk_prop_get_int(&obj->props, "event") > 0) ? 1:0;

	ctx->source = comm_source_register(obj->name, event);

	if (hk_prop_get_int(&obj->props, "private")) {
		comm_source_set_private(ctx->source);
	}

	return obj;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	comm_source_update_str(ctx->source, value);
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
