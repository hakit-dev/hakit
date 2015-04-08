/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>

#include "log.h"
#include "mod.h"


/*
 * HAKit module class definition
 */

static HK_TAB_DECLARE(classes, hk_class_t *);


hk_class_t *hk_class_find(char *name)
{
	int i;

	for (i = 0; i < classes.nmemb; i++) {
		hk_class_t *hc = ((hk_class_t *) classes.buf) + i;
		if (strcmp(hc->name, name) == 0) {
			return hc;
		}
	}

	return NULL;
}


void hk_class_register(hk_class_t *class)
{
	hk_class_t **pclass;

	if (hk_class_find(class->name) != NULL) {
		log_str("ERROR: Class '%s' already exists", class->name);
		return;
	}

	pclass = hk_tab_push(&classes);
	*pclass = class;
}


/*
 * HAKit module pads
 */

hk_pad_t *hk_pad_create(hk_obj_t *obj, char *fmt, ...)
{
	char name[64];
	va_list ap;
	hk_pad_t *pad;

	va_start(ap, fmt);
	vsnprintf(name, sizeof(name), fmt, ap);
	va_end(ap);

	pad = hk_tab_push(&obj->pads);
	pad->obj = obj;
	pad->name = strdup(name);
	pad->state = -1;

	return pad;
}


hk_pad_t *hk_pad_find(hk_obj_t *obj, char *name)
{
	int i;

	for (i = 0; i < obj->pads.nmemb; i++) {
		hk_pad_t *pad = ((hk_pad_t *) obj->pads.buf) + i;
		if (strcmp(pad->name, name) == 0) {
			return pad;
		}
	}

	return NULL;
}


/*
 * HAKit nets
 */

static HK_TAB_DECLARE(nets, hk_net_t);


hk_net_t *hk_net_find(char *name)
{
	int i;

	for (i = 0; i < nets.nmemb; i++) {
		hk_net_t *net = ((hk_net_t *) nets.buf) + i;
		if (strcmp(net->name, name) == 0) {
			return net;
		}
	}

	return NULL;
}


hk_net_t *hk_net_create(char *name)
{
	hk_net_t *net = hk_net_find(name);

	if (net != NULL) {
		log_str("ERROR: Net '%s' already exists", name);
		return NULL;
	}

	net = hk_tab_push(&nets);

	net->name = strdup(name);
	hk_tab_init(&net->ppads, sizeof(hk_pad_t *));

	return net;
}


int hk_net_connect(hk_net_t *net, hk_pad_t *pad)
{
	hk_pad_t **ppad;
	int i;

	/* Ensure pad is not bound to this net */
	for (i = 0; i < net->ppads.nmemb; i++) {
		ppad = &((hk_pad_t **) net->ppads.buf)[i];
		if (*ppad == pad) {
			log_str("WARNING: pad '%s.%s' already connected to net '%s'", pad->obj->name, pad->name, net->name);
			return 1;
		}
	}

	/* Check pad is not already connected */
	if (pad->net != NULL) {
		log_str("ERROR: pad '%s.%s' already connected to net '%s'", pad->obj->name, pad->name, net->name);
		return 0;
	}

	ppad = hk_tab_push(&net->ppads);
	*ppad = pad;
	pad->net = net;

	return 1;
}


/*
 * HAKit objects
 */

static HK_TAB_DECLARE(objs, hk_obj_t);


hk_obj_t *hk_obj_init(hk_obj_t *obj, char *name, hk_class_t *class)
{
	if (obj != NULL) {
		obj->name = strdup(name);
		obj->class = class;
		hk_prop_init(&obj->props);
		hk_tab_init(&obj->pads, sizeof(hk_pad_t));
	}

	return obj;
}


hk_obj_t *hk_obj_create(char *name, hk_class_t *class)
{
	return hk_obj_init(hk_tab_push(&objs), name, class);
}


void hk_obj_prop_set(hk_obj_t *obj, char *name, char *value)
{
	hk_prop_set(&obj->props, name, value);
}


char *hk_obj_prop_get(hk_obj_t *obj, char *name)
{
	return hk_prop_get(&obj->props, name);
}


void hk_obj_prop_foreach(hk_obj_t *obj, hk_prop_foreach_func func, char *user_data)
{
       hk_prop_foreach(&obj->props, func, user_data);
}


#if 0
int hk_obj_connect(hk_obj_t *obj, char *pad_name, char *net_name)
{
	hk_pad_t *pad;
	hk_net_t *net;

	pad = hk_class_pad_find(obj->class, pad_name);
	if (pad == NULL) {
		log_str("ERROR: in object '%s': pad '%s' not found in class '%s'",
			obj->name, pad_name, obj->class->name);
		return -1;
	}

	net = hk_net_find(net_name);
	if (net == NULL) {
		net = hk_net_create(net_name);
	}
	obj->nets[pad->id - 1] = net;

	hk_net_connect(net, obj, pad);

	return 0;
}



void hk_obj_update_str(hk_obj_t *obj, char *pad_name, char *value)
{
	hk_pad_t *pad;
	hk_net_t *net;
	int i;

	/* Find pad entry */
	pad = hk_class_pad_find(obj->class, pad_name);
	if (pad == NULL) {
		log_str("ERROR: in object '%s': pad '%s' not found in class '%s'",
			obj->name, pad_name, obj->class->name);
		return;
	}

	/* Get net connected to this pad */
	net = obj->nets[pad->id - 1];

	/* Update all inputs attached to this net */
	for (i = 0; i < net->targets.nmemb; i++) {
		hk_net_target_t *target = ((hk_net_target_t *) net->targets.buf) + i;
		if ((target->obj != obj) && (target->pad != pad) && (target->pad->func != NULL)) {
			target->pad->func(target->obj, value);
		}
	}
}
#endif
