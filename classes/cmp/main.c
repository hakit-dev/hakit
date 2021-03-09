/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Built-in class : comparator
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


#define CLASS_NAME "cmp"


typedef struct {
	hk_obj_t *obj;
	int hysteresis;
	hk_pad_t *ref;
	hk_pad_t *in;
	hk_pad_t *out;
	int started;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *str;

	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	str = hk_prop_get(&obj->props, "hysteresis");
	if (str != NULL) {
		ctx->hysteresis = atoi(str);
	}

	ctx->ref = hk_pad_create(obj, HK_PAD_IN, "ref");
	ctx->in = hk_pad_create(obj, HK_PAD_IN, "in");

	ctx->out = hk_pad_create(obj, HK_PAD_OUT, "out");
        ctx->out->state = -1;

	return 0;
}


static void _start(hk_obj_t *obj)
{
	ctx_t *ctx = obj->ctx;

        log_debug(2, "cmp(%s) _start %d", obj->name, ctx->out->state);

        ctx->started = 1;

        if (ctx->out->state >= 0) {
                hk_pad_update_int(ctx->out, ctx->out->state);
        }
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
        int out_state = ctx->out->state;

        pad->state = atoi(value);

        if (ctx->in->state <= (ctx->ref->state - ctx->hysteresis)) {
                out_state = 0;
        }
        else if (ctx->in->state > (ctx->ref->state + ctx->hysteresis)) {
                out_state = 1;
        }

        log_debug(2, "cmp(%s) _input %d/%d/%d : %d => %d", pad->obj->name, ctx->ref->state - ctx->hysteresis, ctx->in->state, ctx->ref->state + ctx->hysteresis, ctx->out->state, out_state);

        if ((out_state != ctx->out->state) || (ctx->out->state < 0)) {
                ctx->out->state = out_state;
                if (ctx->started && (ctx->out->state >= 0)) {
                        hk_pad_update_int(ctx->out, out_state);
                }
        }
}


const hk_class_t _class_cmp = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
