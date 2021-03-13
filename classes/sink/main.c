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
#include <malloc.h>

#include "log.h"
#include "mod.h"
#include "endpoint.h"
#include "comm.h"
#include "version.h"


#define CLASS_NAME "sink"

typedef struct {
	hk_obj_t *obj;
	hk_pad_t *output;
        hk_sink_t *sink;
} ctx_t;


static void _event(ctx_t *ctx, hk_ep_t *ep)
{
	hk_pad_update_str(ctx->output, hk_ep_get_value(ep));
}


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;

	ctx = malloc(sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->output = hk_pad_create(obj, HK_PAD_OUT, "out");

	int local = (hk_prop_get(&obj->props, "local") != NULL) ? 1:0;
	ctx->sink = comm_sink_register(obj, local, (hk_ep_func_t) _event, ctx);

        char *widget = hk_prop_get(&obj->props, "widget");
        if (widget != NULL) {
                hk_ep_set_widget(HK_EP(ctx->sink), widget);
        }

        char *chart = hk_prop_get(&obj->props, "chart");
        if (chart != NULL) {
                hk_ep_set_chart(HK_EP(ctx->sink), chart);
        }

	return 0;
}

static void _start(hk_obj_t *obj)
{
	ctx_t *ctx = obj->ctx;

	if (ctx->output->value.base != NULL) {
		comm_sink_update_str(ctx->sink, (char *) ctx->output->value.base);
	}
}


const hk_class_t _class_sink = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
};
