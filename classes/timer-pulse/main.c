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
#include "sys.h"
#include "mod.h"

#include "version.h"


#define CLASS_NAME "timer-pulse"

#define	EDGE_RAISING 0x01
#define	EDGE_FALLING 0x02


typedef struct {
	hk_obj_t *obj;
	hk_pad_t *input;
	hk_pad_t *output;
	int edge;
	int retrigger;
	int delay;
	int inv;
	sys_tag_t timeout_tag;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *str;

	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->input = hk_pad_create(obj, HK_PAD_IN, "in");
	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	ctx->edge = EDGE_RAISING;
	str = hk_prop_get(&obj->props, "edge");
	if (str != NULL) {
		if (strcmp(str, "falling") == 0) {
			ctx->edge = EDGE_FALLING;
		}
		else if (strcmp(str, "both") == 0) {
			ctx->edge = EDGE_RAISING | EDGE_FALLING;
		}
	}

	str = hk_prop_get(&obj->props, "retrigger");
	if (str != NULL) {
		ctx->retrigger = 1;
	}

	ctx->delay = hk_prop_get_int(&obj->props, "delay");

	str = hk_prop_get(&obj->props, "inv");
	if (str != NULL) {
		ctx->inv = 1;
	}

	return 0;
}


static int timeout(ctx_t *ctx)
{
	ctx->timeout_tag = 0;
	ctx->output->state = 0;
	hk_pad_update_int(ctx->output, ctx->inv);
	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	int state0 = pad->state;

	pad->state = atoi(value) ? 1:0;

	if (((ctx->edge | EDGE_RAISING) && (state0 == 0) && (pad->state == 1)) ||
	    ((ctx->edge | EDGE_FALLING) && (state0 == 1) && (pad->state == 0))) {
		if ((ctx->output->state == 0) || ctx->retrigger) {
			if (ctx->timeout_tag != 0) {
				sys_remove(ctx->timeout_tag);
			}

			ctx->timeout_tag = sys_timeout(ctx->delay, (sys_func_t) timeout, ctx);

			if (ctx->output->state == 0) {
				ctx->output->state = 1;
				hk_pad_update_int(ctx->output, 1 ^ ctx->inv);
			}
		}
	}
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
