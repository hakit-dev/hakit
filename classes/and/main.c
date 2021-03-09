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


#define CLASS_NAME "and"

typedef struct {
	hk_obj_t *obj;
	int ninputs;
        int inv;
	hk_pad_t **inputs;
	hk_pad_t *output;
	int refresh;
} ctx_t;


static int add_input(void *user_data, char *name, char *value)
{
	hk_obj_t *obj = user_data;
	ctx_t *ctx = obj->ctx;
        int num = -1;
        int i;

        if ((sscanf(name, "in%d", &num) == 1) && (num >= 0)) {
                int ninputs2 = num + 1;
                if (ninputs2 > ctx->ninputs) {
                        log_debug(2, CLASS_NAME ": grow input table %d -> %d", ctx->ninputs, ninputs2);
                        ctx->inputs = realloc(ctx->inputs, sizeof(hk_pad_t *)*ninputs2);
                        for (i = ctx->ninputs; i < ninputs2; i++) {
                                ctx->inputs[i] = NULL;
                        }
                        ctx->ninputs = ninputs2;
                }
		ctx->inputs[num] = hk_pad_create(obj, HK_PAD_IN, name);
                log_str(CLASS_NAME ": add input '%s'", name);
        }

        return 1;
}


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	if (hk_prop_get(&obj->props, "inv") != NULL) {
                ctx->inv = 1;
                log_str(CLASS_NAME ": inverted output, NAND gate mode enabled");
        }

        hk_prop_foreach(&obj->props, add_input, (void *) obj);
        log_str(CLASS_NAME ": %d inputs probed", ctx->ninputs);

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	ctx->refresh = 1;

	return 0;
}


static void _update(ctx_t *ctx)
{
	int out_state = 1;
	int i;

	for (i = 0; i < ctx->ninputs; i++) {
		if ((ctx->inputs[i] != NULL) && (ctx->inputs[i]->state == 0)) {
			out_state = 0;
			break;
		}
	}

	if (ctx->inv) {
		out_state ^= 1;
	}

	if ((out_state != ctx->output->state) || ctx->refresh) {
		ctx->refresh = 0;
		ctx->output->state = out_state;
		hk_pad_update_int(ctx->output, out_state);
	}
}


static void _input(hk_pad_t *pad, char *value)
{
	pad->state = atoi(value) ? 1:0;
        _update(pad->obj->ctx);
}


static void _start(hk_obj_t *obj)
{
        _update(obj->ctx);
}


const hk_class_t _class_and = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
