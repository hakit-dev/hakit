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
#include "buf.h"
#include "mod.h"


/*
 * HAKit module class definition
 */

static HK_TAB_DECLARE(classes, hk_class_t *);

#define HK_CLASS_ENTRY(i) HK_TAB_VALUE(classes, hk_class_t *, i)


hk_class_t *hk_class_find(char *name)
{
	int i;

	for (i = 0; i < classes.nmemb; i++) {
		hk_class_t *hc = HK_CLASS_ENTRY(i);
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

hk_pad_t *hk_pad_create(hk_obj_t *obj, hk_pad_dir_t dir, char *fmt, ...)
{
	char name[64];
	va_list ap;
	hk_pad_t **ppad;
	hk_pad_t *pad;

	va_start(ap, fmt);
	vsnprintf(name, sizeof(name), fmt, ap);
	va_end(ap);

	pad = (hk_pad_t *) malloc(sizeof(hk_pad_t));
	memset(pad, 0, sizeof(hk_pad_t));
	pad->obj = obj;
	pad->dir = dir;
	pad->name = strdup(name);
	buf_init(&pad->value);

	log_debug(2, "hk_pad_create %s.%s", obj->name, pad->name);

	ppad = hk_tab_push(&obj->pads);
	*ppad = pad;

	return pad;
}


hk_pad_t *hk_pad_find(hk_obj_t *obj, char *name)
{
	int i;

	for (i = 0; i < obj->pads.nmemb; i++) {
		hk_pad_t *pad = HK_TAB_VALUE(obj->pads, hk_pad_t *, i);
		if (strcmp(pad->name, name) == 0) {
			return pad;
		}
	}

	return NULL;
}


static void hk_pad_update_input(hk_pad_t *pad, char *value)
{
	if (pad->lock) {
		log_str("WARNING: Attempting to update locked input %s.%s",
			pad->obj->name, pad->name);
	}
	else {
		if (pad->obj->class->input != NULL) {
			log_debug(2, "  -> %s.%s", pad->obj->name, pad->name);
			pad->lock = 1;
			pad->obj->class->input(pad, value);
			pad->lock = 0;
		}
	}
}


void hk_pad_update_str(hk_pad_t *pad, char *value)
{
	hk_net_t *net = pad->net;
	int i;

	log_debug(2, "hk_pad_update_str %s.%s='%s'", pad->obj->name, pad->name, value);

	buf_set_str(&pad->value, value);

	/* Do nothing if no net is connected to this pad */
	if (net == NULL) {
		return;
	}

	/* Raise lock to detect loop references */
	pad->lock = 1;

	for (i = 0; i < net->pads.nmemb; i++) {
		hk_pad_t *pad2 = HK_TAB_VALUE(net->pads, hk_pad_t *, i);

		/* Consider all input pads bound to the net */
		if ((pad2 != pad) && (pad2->dir != HK_PAD_OUT)) {
			hk_pad_update_input(pad2, value);
		}
	}

	/* Free loop reference lock */
	pad->lock = 0;
}


void hk_pad_update_int(hk_pad_t *pad, int value)
{
	char str[20];

	snprintf(str, sizeof(str), "%d", value);
	hk_pad_update_str(pad, str);
}


char *hk_pad_get_value(char *name)
{
	char *pt;
	hk_obj_t *obj;
	char *value = NULL;

	// Fully qualified pad name expected: <obj_name>.<pad_name>
	pt = strchr(name, '.');
	if (pt != NULL) {
		*pt = '\0';
		obj = hk_obj_find(name);
		*pt = '.';

		if (obj != NULL) {
			hk_pad_t *pad = hk_pad_find(obj, pt+1);
			if (pad != NULL) {
				value = (char *) pad->value.base;
			}
		}
	}

	return value;
}


/*
 * HAKit nets
 */

static HK_TAB_DECLARE(nets, hk_net_t *);

#define HK_NET_ENTRY(i) HK_TAB_VALUE(nets, hk_net_t *, i)


hk_net_t *hk_net_find(char *name)
{
	int i;

	for (i = 0; i < nets.nmemb; i++) {
		hk_net_t *net = HK_NET_ENTRY(i);
		if (strcmp(net->name, name) == 0) {
			return net;
		}
	}

	return NULL;
}


hk_net_t *hk_net_create(char *name)
{
	hk_net_t *net = hk_net_find(name);
	hk_net_t **pnet;

	if (net != NULL) {
		log_str("ERROR: Net '%s' already exists", name);
		return NULL;
	}

	log_debug(2, "Creating net '%s'", name);

	net = (hk_net_t *) malloc(sizeof(hk_net_t));
	memset(net, 0, sizeof(hk_net_t));
	net->name = strdup(name);
	hk_tab_init(&net->pads, sizeof(hk_pad_t *));

	pnet = hk_tab_push(&nets);
	*pnet = net;

	return net;
}


int hk_net_connect(hk_net_t *net, hk_pad_t *pad)
{
	hk_pad_t **ppad;
	int i;

	/* Ensure pad is not bound to this net */
	for (i = 0; i < net->pads.nmemb; i++) {
		hk_pad_t *pad2 = HK_TAB_VALUE(net->pads, hk_pad_t *, i);
		if (pad2 == pad) {
			log_str("WARNING: pad '%s.%s' already connected to net '%s'", pad->obj->name, pad->name, net->name);
			return 0;
		}
	}

	/* Check pad is not already connected */
	if (pad->net != NULL) {
		log_str("ERROR: pad '%s.%s' already connected to net '%s'", pad->obj->name, pad->name, net->name);
		return 0;
	}

	log_debug(2, "Connecting pad '%s.%s' to net '%s'", pad->obj->name, pad->name, net->name);

	ppad = hk_tab_push(&net->pads);
	*ppad = pad;
	pad->net = net;

	return 1;
}


/*
 * HAKit objects
 */

static HK_TAB_DECLARE(objs, hk_obj_t *);

#define HK_OBJ_ENTRY(i) HK_TAB_VALUE(objs, hk_obj_t *, i)


hk_obj_t *hk_obj_find(char *name)
{
	int i;

	for (i = 0; i < objs.nmemb; i++) {
		hk_obj_t *obj = HK_OBJ_ENTRY(i);
		if (strcmp(obj->name, name) == 0) {
			return obj;
		}
	}

	return NULL;
}


hk_obj_t *hk_obj_create(hk_class_t *class, char *name, int argc, char **argv)
{
	hk_obj_t *obj;
	hk_obj_t **pobj;
	int i;

	obj = hk_obj_find(name);
	if (obj != NULL) {
		log_str("ERROR: Object '%s' already exists", name);
		return NULL;
	}

	log_debug(2, "Creating object '%s' from class '%s'", name, class->name);

	obj = (hk_obj_t *) malloc(sizeof(hk_obj_t));
	obj->name = strdup(name);
	obj->class = class;
	hk_prop_init(&obj->props);
	hk_tab_init(&obj->pads, sizeof(hk_pad_t *));
	obj->ctx = NULL;

	for (i = 0; i < argc; i++) {
		char *args = argv[i];
		char *eq = strchr(args, '=');
		char *value = "";

		if (eq != NULL) {
			*eq = '\0';
			value = eq+1;
		}

		hk_obj_prop_set(obj, args, value);

		if (eq != NULL) {
			*eq = '=';
		}
	}

	pobj = hk_tab_push(&objs);
	*pobj = obj;

	return obj;
}


static int hk_obj_preset(hk_obj_t *obj, char *name, char *value)
{
	hk_pad_t *pad = hk_pad_find(obj, name);

	if ((pad != NULL) && (value != NULL)) {
		log_debug(2, "hk_obj_preset %s.%s='%s'", obj->name, name, value);

		if (pad->dir == HK_PAD_OUT) {
			hk_pad_update_str(pad, value);
		}
		else {
			buf_set_str(&pad->value, value);
			hk_pad_update_input(pad, value);
		}
	}

	return 1;
}


void hk_obj_start_all(void)
{
	int i;

	for (i = 0; i < objs.nmemb; i++) {
		hk_obj_t *obj = HK_OBJ_ENTRY(i);

		/* Configure input/output presets */
		hk_prop_foreach(&obj->props, (hk_prop_foreach_func) hk_obj_preset, (void *) obj);

		/* Invoke start handler */
		if (obj->class->start != NULL) {
			log_debug(2, "Starting object '%s'", obj->name);
			obj->class->start(obj);
		}
	}
}


void hk_obj_prop_set(hk_obj_t *obj, char *name, char *value)
{
	log_debug(2, "Setting property for object '%s': %s='%s'", obj->name, name, value);
	hk_prop_set(&obj->props, name, value);
}


char *hk_obj_prop_get(hk_obj_t *obj, char *name)
{
	return hk_prop_get(&obj->props, name);
}


int hk_obj_prop_get_int(hk_obj_t *obj, char *name)
{
	return hk_prop_get_int(&obj->props, name);
}


void hk_obj_prop_foreach(hk_obj_t *obj, hk_prop_foreach_func func, void *user_data)
{
       hk_prop_foreach(&obj->props, func, user_data);
}
