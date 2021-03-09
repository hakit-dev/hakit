/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2015 Sylvain Giroudon
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
#include "history.h"

#include "version.h"


#define CLASS_NAME "history"

typedef struct {
	hk_pad_t *pad;
} input_t;

typedef struct {
	hk_obj_t *obj;
	history_t h;
	hk_tab_t inputs;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *s1;
	int last_id = 0;

	/* Create object context */
	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	/* Create history context */
	history_init(&ctx->h);

	/* Create input pads */
	hk_tab_init(&ctx->inputs, sizeof(input_t));
	s1 = hk_prop_get(&obj->props, "inputs");
	while (s1 != NULL) {
		input_t *pinput;

		char *s2 = strchr(s1, ',');
		if (s2 != NULL) {
			*(s2++) = '\0';
		}

		pinput = hk_tab_push(&ctx->inputs);
		pinput->pad = hk_pad_create(obj, HK_PAD_IN, s1);

		last_id++;
		history_signal_declare(&ctx->h, last_id, s1);
		pinput->pad->state = last_id;

		s1 = s2;
	}


	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

	history_feed(&ctx->h, pad->state, value);
}


const hk_class_t _class_history = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
