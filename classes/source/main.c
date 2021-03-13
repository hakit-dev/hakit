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


#define CLASS_NAME "source"

typedef struct {
	hk_obj_t *obj;
	hk_pad_t *input;
        hk_source_t *source;
} ctx_t;


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;

	ctx = malloc(sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	ctx->input = hk_pad_create(obj, HK_PAD_IN, "in");
	int local = (hk_prop_get(&obj->props, "local") != NULL) ? 1:0;
	int event = (hk_prop_get(&obj->props, "event") != NULL) ? 1:0;

	ctx->source = comm_source_register(obj, local, event);

        char *widget = hk_prop_get(&obj->props, "widget");
        if (widget != NULL) {
                hk_ep_set_widget(HK_EP(ctx->source), widget);
        }

        char *chart = hk_prop_get(&obj->props, "chart");
        if (chart != NULL) {
                hk_ep_set_chart(HK_EP(ctx->source), chart);
        }

	return 0;
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	comm_source_update_str(ctx->source, value);
}


const hk_class_t _class_source = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
