/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <malloc.h>

#include "log.h"
#include "mod.h"


#define CLASS_NAME "and"

typedef struct {
	hk_obj_t *obj;
	int ninputs;
	hk_pad_t **inputs;
	hk_pad_t *output;
} ctx_t;


static hk_obj_t *_new(hk_obj_t *obj)
{
	ctx_t *ctx;
	int ninputs;
	int i;

	ninputs = hk_prop_get_int(&obj->props, "ninputs");
	if (ninputs < 2) {
		log_str("ERROR: Class '" CLASS_NAME "': cannot instanciate object '%s' with less than 2 inputs", obj->name);
		return NULL;
	}

	ctx = malloc(sizeof(ctx_t));
	ctx->obj = obj;
	ctx->ninputs = ninputs;
	ctx->inputs = calloc(ninputs, sizeof(hk_pad_t *));
	obj->ctx = ctx;

	for (i = 0; i < ninputs; i++) {
		ctx->inputs[i] = hk_pad_create(obj, "in%d", i+1);
	}

	ctx->output = hk_pad_create(obj, "out");

	return obj;
}


static void _input(hk_pad_t *pad, char *value)
{
}


hk_class_t hk_class_and = {
	.name = CLASS_NAME,
	.new = _new,
	.input = _input,
};
