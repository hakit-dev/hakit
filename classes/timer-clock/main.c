/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Timer class : clock
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


#define CLASS_NAME "timer-clock"

#define	EDGE_RAISING 0x01
#define	EDGE_FALLING 0x02


typedef struct {
	hk_obj_t *obj;
	hk_pad_t *enable;
	hk_pad_t *output;
	int period;
	int delay;
	sys_tag_t period_tag;
	sys_tag_t duty_tag;
} ctx_t;


static int duty_handler(ctx_t *ctx)
{
	ctx->duty_tag = 0;
	ctx->output->state = 0;
	hk_pad_update_int(ctx->output, 0);
	return 0;
}


static int period_handler(ctx_t *ctx)
{
	ctx->output->state = 1;
	hk_pad_update_int(ctx->output, 1);

	ctx->duty_tag = sys_timeout(ctx->delay, (sys_func_t) duty_handler, ctx);

	return 1;
}


static void enable(ctx_t *ctx)
{
	if (ctx->period_tag == 0) {
		ctx->period_tag = sys_timeout(ctx->period, (sys_func_t) period_handler, ctx);
	}

	period_handler(ctx);
}


static int _new(hk_obj_t *obj)
{
	int period;
	int delay;
	ctx_t *ctx;
	char *str;

	/*  Get period value in milliseconds */
	str = hk_prop_get(&obj->props, "period");
	if (str == NULL) {
		log_str("ERROR: Missing property 'period' for object '%s'", obj->name);
		return -1;
	}

	period = atoi(str);
	if (period < 2) {
		log_str("ERROR: Illegal period value for object '%s': Must be >= 2 ms.", obj->name);
		return -1;
	}

	/*  Get duty value in % */
	str = hk_prop_get(&obj->props, "duty");
	if (str != NULL) {
		char *percent = strchr(str, '%');

		if (percent != NULL) {
			*percent = '\0';
			int duty = atoi(str);
			if ((duty <= 0) || (duty >= 100)) {
				log_str("ERROR: Illegal duty percent value for object '%s': Must be 1..99 %%.", obj->name);
				return -1;
			}
			delay = (period * duty) / 100;
		}
		else {
			delay = atoi(str);
		}
	}
	else {
		delay = period / 2;
	}

	/* Clamp minimum delay value to 1 ms */
	if (delay < 1) {
		delay = 1;
	}

	/* Clamp maximum delay value to (period-1) ms */
	if (delay >= period) {
		delay = period - 1;
	}

	/* Create object context */
	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->enable = hk_pad_create(obj, HK_PAD_IN, "enable");
	ctx->enable->state = -1;

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	ctx->period = period;
	ctx->delay = delay;

	log_debug(1, CLASS_NAME "(%s): period=%dms duty=%dms", obj->name, period, ctx->delay);

	return 0;
}


static void _start(hk_obj_t *obj)
{
	ctx_t *ctx = obj->ctx;
	hk_pad_update_int(ctx->output, 0);
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

	pad->state = atoi(value) ? 1:0;
	log_debug(1, CLASS_NAME "(%s): %s=%d.", pad->obj->name, pad->name, pad->state);

	if (ctx->period_tag != 0) {
		sys_remove(ctx->period_tag);
		ctx->period_tag = 0;
	}

	if (ctx->duty_tag != 0) {
		sys_remove(ctx->duty_tag);
		ctx->duty_tag = 0;
	}

	if (pad->state) {
		enable(ctx);
	}
	else {
		duty_handler(ctx);
	}
}


hk_class_t _class_timer_clock = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
