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
	int refresh;
} ctx_t;


static int is_inverted(char *prop_inv, char *name)
{
	char *s1 = prop_inv;

	while (s1 != NULL) {
		char *s2 = strchr(s1, ',');
		if (s2 != NULL) {
			*(s2++) = '\0';
		}

		if (strcmp(s1, name) == 0) {
			return 1;
		}

		s1 = s2;
	}

	return 0;
}


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *str;
	char *prop_inv;
	int ninputs = 1;
	int i;

	str = hk_prop_get(&obj->props, "ninputs");
	if (str != NULL) {
		ninputs = atoi(str);
		if (ninputs < 1) {
			log_str("ERROR: Class '" CLASS_NAME "': cannot instanciate object '%s' with less than 1 input", obj->name);
			return -1;
		}
	}

	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	ctx->ninputs = ninputs;
	ctx->inputs = calloc(ninputs, sizeof(hk_pad_t *));
	obj->ctx = ctx;

	prop_inv = hk_prop_get(&obj->props, "inv");

	for (i = 0; i < ninputs; i++) {
		hk_pad_t *input = hk_pad_create(obj, HK_PAD_IN, "in%d", i);
		if (is_inverted(prop_inv, input->name)) {
			input->state = 2;
		}
		ctx->inputs[i] = input;
	}

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");
	if (is_inverted(prop_inv, ctx->output->name)) {
		ctx->output->state = 2;
	}

	ctx->refresh = 1;

	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	static const int tt[4] = {0,1,1,0};
	ctx_t *ctx = pad->obj->ctx;
	int in_state = atoi(value) ? 1:0;
	int out_state = 0;
	int i;

	pad->state = (pad->state & ~1) | in_state;

	for (i = 0; i < ctx->ninputs; i++) {
		if (tt[ctx->inputs[i]->state & 3]) {
			out_state = 1;
			break;
		}
	}

	if (ctx->output->state & 2) {
		out_state ^= 1;
	}

	if ((out_state != (ctx->output->state & 1)) || ctx->refresh) {
		ctx->refresh = 0;
		ctx->output->state = (ctx->output->state & ~1) | out_state;
		hk_pad_update_int(ctx->output, out_state);
	}
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
