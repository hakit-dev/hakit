/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014-2016 Sylvain Giroudon
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


#define CLASS_NAME "mux"


typedef struct {
	hk_obj_t *obj;
	int ninputs;
	hk_pad_t *sel;
	hk_pad_t **inputs;
	hk_pad_t *output;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	int ninputs = 1;
	char *str;
	int i;

	str = hk_prop_get(&obj->props, "ninputs");
	if (str != NULL) {
		ninputs = atoi(str);
		if (ninputs < 2) {
			log_str("ERROR: Class '" CLASS_NAME "': cannot instanciate object '%s' with less than 2 inputs", obj->name);
			return -1;
		}
	}

	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	ctx->ninputs = ninputs;
	ctx->inputs = calloc(ninputs, sizeof(hk_pad_t *));
	obj->ctx = ctx;

	ctx->sel = hk_pad_create(obj, HK_PAD_IN, "sel");

	for (i = 0; i < ninputs; i++) {
		ctx->inputs[i] = hk_pad_create(obj, HK_PAD_IN, "in%d", i);
	}

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	return 0;
}


static void update_output(ctx_t *ctx)
{
	int sel = ctx->sel->state;

	if ((sel >= 0) && (sel < ctx->ninputs)) {
		char *str = (char *) ctx->inputs[sel]->value.base;
		if (str != NULL) {
			hk_pad_update_str(ctx->output, str);
		}
	}
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

	/* Set selector value */
	if (pad == ctx->sel) {
		pad->state = atoi(value);
	}

	/* Update output according to selector value */
	update_output(ctx);
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
