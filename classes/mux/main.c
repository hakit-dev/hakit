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

	ctx->sel = hk_pad_create(obj, HK_PAD_IN, "sel");

        hk_prop_foreach(&obj->props, add_input, (void *) obj);
        log_str(CLASS_NAME ": %d inputs probed", ctx->ninputs);

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	return 0;
}


static void update_output(ctx_t *ctx)
{
	int sel = ctx->sel->state;

	log_debug(3, CLASS_NAME ".update_output: sel=%d", sel);

	if ((sel >= 0) && (sel < ctx->ninputs) && (ctx->inputs[sel] != NULL)) {
		char *str = (char *) ctx->inputs[sel]->value.base;
		log_debug(3, "  -> '%s'", str);
		if (str != NULL) {
			hk_pad_update_str(ctx->output, str);
		}
	}
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

	log_debug(3, CLASS_NAME "._input: %s.%s=\"%s\"", pad->obj->name, pad->name, value);

	/* Set selector value */
	if (pad == ctx->sel) {
		pad->state = atoi(value);
	}
	else {
		hk_pad_update_str(pad, value);
	}

	/* Update output according to selector value */
	update_output(ctx);
}


static void _start(hk_obj_t *obj)
{
	update_output(obj->ctx);
}


const hk_class_t _class_mux = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
