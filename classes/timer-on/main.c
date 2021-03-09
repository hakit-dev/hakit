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


#define CLASS_NAME "timer-on"


typedef struct {
	hk_obj_t *obj;
	hk_pad_t *input;
	hk_pad_t *output;
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

	ctx->delay = hk_prop_get_int(&obj->props, "delay");

	str = hk_prop_get(&obj->props, "inv");
	if (str != NULL) {
		ctx->inv = 1;
	}

	return 0;
}


static void set_output(ctx_t *ctx)
{
	hk_pad_update_int(ctx->output, ctx->output->state ^ ctx->inv);
}


static void _start(hk_obj_t *obj)
{
	set_output(obj->ctx);
}


static int timeout(ctx_t *ctx)
{
	ctx->timeout_tag = 0;
	ctx->output->state = 1;
	set_output(ctx);
	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	int state0 = pad->state;

	pad->state = atoi(value) ? 1:0;

	if (pad->state) {
		if (state0 == 0) {
			if (ctx->timeout_tag != 0) {
				sys_remove(ctx->timeout_tag);
			}

			ctx->timeout_tag = sys_timeout(ctx->delay, (sys_func_t) timeout, ctx);
		}
	}
	else {
		if (ctx->timeout_tag != 0) {
			sys_remove(ctx->timeout_tag);
			ctx->timeout_tag = 0;
		}

		if (ctx->output->state) {
			ctx->output->state = 0;
			set_output(ctx);
		}
	}
}


const hk_class_t _class_timer_on = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
