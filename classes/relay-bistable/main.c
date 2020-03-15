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


#define CLASS_NAME "relay-bistable"

#define PULSE 1000   // Pulse width in ms

typedef struct {
	hk_obj_t *obj;
	hk_pad_t *input;
	hk_pad_t *output[2];
	sys_tag_t timeout_tag;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;

	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->input = hk_pad_create(obj, HK_PAD_IN, "in");
	ctx->output[0] = hk_pad_create(obj, HK_PAD_OUT, "out0");
	ctx->output[1] = hk_pad_create(obj, HK_PAD_OUT, "out1");

	return 0;
}


static int timeout(ctx_t *ctx)
{
	ctx->timeout_tag = 0;
        ctx->output[0]->state = 0;
        hk_pad_update_int(ctx->output[0], 0);
        ctx->output[1]->state = 0;
        hk_pad_update_int(ctx->output[1], 0);
	return 0;
}


static void set_output(ctx_t *ctx, int state)
{
        if (ctx->timeout_tag != 0) {
                sys_remove(ctx->timeout_tag);
        }

        if (state) {
                ctx->output[0]->state = 0;
                hk_pad_update_int(ctx->output[0], 0);
                ctx->output[1]->state = 1;
                hk_pad_update_int(ctx->output[1], 1);
        }
        else {
                ctx->output[0]->state = 1;
                hk_pad_update_int(ctx->output[0], 1);
                ctx->output[1]->state = 0;
                hk_pad_update_int(ctx->output[1], 0);
        }

        ctx->timeout_tag = sys_timeout(PULSE, (sys_func_t) timeout, ctx);
}


static void _start(hk_obj_t *obj)
{
	set_output(obj->ctx, 0);
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	int state0 = pad->state;

	pad->state = atoi(value) ? 1:0;

	if (pad->state != state0) {
                set_output(ctx, pad->state);
	}
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
