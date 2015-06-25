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


#define CLASS_NAME "or"

typedef struct {
	hk_obj_t *obj;
	int ninputs;
	hk_pad_t **inputs;
	hk_pad_t *output;
	int inv;
	int refresh;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *str;
	int ninputs;
	int i;

	ninputs = hk_prop_get_int(&obj->props, "ninputs");
	if (ninputs < 2) {
		log_str("ERROR: Class '" CLASS_NAME "': cannot instanciate object '%s' with less than 2 inputs", obj->name);
		return -1;
	}

	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	ctx->ninputs = ninputs;
	ctx->inputs = calloc(ninputs, sizeof(hk_pad_t *));
	obj->ctx = ctx;

	for (i = 0; i < ninputs; i++) {
		ctx->inputs[i] = hk_pad_create(obj, HK_PAD_IN, "in%d", i+1);
	}

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	str = hk_prop_get(&obj->props, "inv");
	if (str != NULL) {
		ctx->inv = 1;
	}

	ctx->refresh = 1;

	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	int state = 0;
	int i;

	pad->state = atoi(value) ? 1:0;

	for (i = 0; i < ctx->ninputs; i++) {
		if (ctx->inputs[i]->state != 0) {
			state = 1;
			break;
		}
	}

	if ((state != ctx->output->state) || ctx->refresh) {
		ctx->refresh = 0;

		if (ctx->inv) {
			state = 1 - state;
		}

		ctx->output->state = state;
		hk_pad_update_int(ctx->output, state);
	}
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
