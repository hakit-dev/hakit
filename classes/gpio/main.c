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
#include "sys.h"
#include "mod.h"
#include "gpio.h"

#include "version.h"


#define CLASS_NAME "gpio"

typedef struct {
	hk_obj_t *obj;
	int port;
	int input;
	hk_pad_t *pad;
	hk_pad_t *blink;
	int value;
	int debounce_delay;
	sys_tag_t timeout_tag;
} ctx_t;


static int input_update(ctx_t *ctx)
{
	ctx->timeout_tag = 0;

	if (ctx->pad->state != ctx->value) {
		ctx->pad->state = ctx->value;
		hk_pad_update_int(ctx->pad, ctx->value);
	}

	return 0;
}


static void timeout_clear(ctx_t *ctx)
{
	if (ctx->timeout_tag != 0) {
		sys_remove(ctx->timeout_tag);
		ctx->timeout_tag = 0;
	}
}


static void input_changed(ctx_t *ctx, int n, int value)
{
	log_debug(2, "%s: gpio[%d] = %d", ctx->obj->name, n, value);

	ctx->value = value;

	/* Update pad event after debounce delay */
        timeout_clear(ctx);

	if (ctx->debounce_delay > 0) {
		ctx->timeout_tag = sys_timeout(ctx->debounce_delay, (sys_func_t) input_update, ctx);
	}
	else {
		input_update(ctx);
	}
}


static int blink_timer(ctx_t *ctx)
{
	ctx->pad->state = 1 - ctx->pad->state;
	gpio_set_value(ctx->port, ctx->pad->state);
        return 1;
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

	/* Get GPIO port configuration */
	if (*str == '!') {
		str++;
		active_low = 1;
	}

	ctx->port = atoi(str);
	ctx->input = input;
	ctx->value = -1;

	/* Get debounce delay */
	ctx->debounce_delay = hk_prop_get_int(&obj->props, "debounce");

	/* Setup GPIO port */
	gpio_export(ctx->port);
	gpio_set_active_low(ctx->port, active_low);

	if (input) {
		gpio_set_input(ctx->port, (gpio_input_func_t) input_changed, ctx);
		ctx->pad = hk_pad_create(obj, HK_PAD_OUT, "out");
	}
	else {
		gpio_set_output(ctx->port);
		ctx->pad = hk_pad_create(obj, HK_PAD_IN, "in");
                ctx->blink = hk_pad_create(obj, HK_PAD_IN, "blink");
	}

	return 0;
}


static void _start(hk_obj_t *obj)
{
	ctx_t *ctx = obj->ctx;

	if (ctx->input) {
		ctx->value = gpio_get_value(ctx->port);
		ctx->pad->state = ctx->value;
		hk_pad_update_int(ctx->pad, ctx->value);
	}
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

        timeout_clear(ctx);

        pad->state = atoi(value);

        if (pad == ctx->pad) {
                if (ctx->pad->state != 0) {
                        ctx->pad->state = 1;
                        if (ctx->blink->state > 0) {
                                ctx->timeout_tag = sys_timeout(ctx->blink->state, (sys_func_t) blink_timer, ctx);
                        }
                }
                log_debug(2, "%s: gpio[%d] = %d", ctx->obj->name, ctx->port, ctx->pad->state);

                ctx->value = ctx->pad->state;
                gpio_set_value(ctx->port, ctx->pad->state);
        }
        else {
                if (ctx->blink->state > 0) {
                        if (ctx->value > 0) {
                                ctx->timeout_tag = sys_timeout(ctx->blink->state, (sys_func_t) blink_timer, ctx);
                        }
                }
                else {
                        if ((ctx->value >= 0) && (ctx->pad->state != ctx->value)) {
                                ctx->pad->state = ctx->value;
                                gpio_set_value(ctx->port, ctx->pad->state);
                        }
                }
        }
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
