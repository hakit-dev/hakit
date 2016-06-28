/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2016 Sylvain Giroudon
 *
 * GPIO access class
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
#include "gpio.h"

#include "version.h"


#define CLASS_NAME "gpio"

typedef struct {
	hk_obj_t *obj;
	int port;
	int input;
	hk_pad_t *pad;
} ctx_t;


static void input_changed(ctx_t *ctx, int n, int value)
{
	log_debug(2, "%s: gpio[%d] = %d", ctx->obj->name, n, value);
	ctx->pad->state = value;
	hk_pad_update_int(ctx->pad, ctx->pad->state);
}


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *str;
	int active_low = 0;
	int input = 0;

	/* Get gpio port number */
	str = hk_prop_get(&obj->props, "input");
	if (str != NULL) {
		input = 1;
	}
	else {
		str = hk_prop_get(&obj->props, "output");
		if (str == NULL) {
			log_str("ERROR: Missing gpio port property for object '%s'", obj->name);
			return -1;
		}
	}

	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	if (*str == '!') {
		str++;
		active_low = 1;
	}

	ctx->port = atoi(str);
	ctx->input = input;

	gpio_export(ctx->port);
	gpio_set_active_low(ctx->port, active_low);

	if (input) {
		gpio_set_input(ctx->port, (gpio_input_func_t) input_changed, ctx);
		ctx->pad = hk_pad_create(obj, HK_PAD_OUT, "out");
	}
	else {
		gpio_set_output(ctx->port);
		ctx->pad = hk_pad_create(obj, HK_PAD_IN, "in");
	}

	return 0;
}


static void _start(hk_obj_t *obj)
{
	ctx_t *ctx = obj->ctx;

	if (ctx->input) {
		int value = gpio_get_value(ctx->port);
		input_changed(ctx, ctx->port, value);
	}
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

	pad->state = atoi(value);
	gpio_set_value(ctx->port, pad->state);
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
