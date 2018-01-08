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
	int id;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	int local, event;

	ctx = malloc(sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->input = hk_pad_create(obj, HK_PAD_IN, "in");
	local = (hk_prop_get(&obj->props, "local") != NULL) ? 1:0;
	event = (hk_prop_get(&obj->props, "event") != NULL) ? 1:0;

	ctx->id = comm_source_register(obj, local, event);

	comm_source_set_widget(ctx->id, hk_prop_get(&obj->props, "widget"));
	comm_source_set_chart(ctx->id, hk_prop_get(&obj->props, "chart"));

	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	comm_source_update_str(ctx->id, value);
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
