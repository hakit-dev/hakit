#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "log.h"
#include "mod.h"


/*
 * HAKit module class definition
 */

static HK_TAB_DECLARE(classes, hk_class_t);


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


hk_class_t *hk_class_create(char *name)
{
	hk_class_t *hc = hk_class_find(name);

	if (hc != NULL) {
		log_str("ERROR: Class '%s' already exists", name);
		return NULL;
	}

	hc = hk_tab_push(&classes);

	hc->name = strdup(name);
	hk_prop_init(&hc->props);
	hk_tab_init(&hc->pads, sizeof(hk_pad_t));

	return hc;
}


hk_pad_t *hk_class_pad_add(hk_class_t *hc, char *name, hk_pad_input_func func)
{
	hk_pad_t *pad = hk_tab_push(&hc->pads);

	pad->id = hc->pads.nmemb;
	pad->name = strdup(name);
	pad->func = func;

	return pad;
}


void hk_class_pad_foreach(hk_class_t *hc, hk_class_pad_foreach_func func, void *user_data)
{
	hk_tab_foreach(&hc->pads, (hk_tab_foreach_func) func, user_data);
}


hk_pad_t *hk_class_pad_find(hk_class_t *hc, char *name)
{
	int i;

	for (i = 0; i < hc->pads.nmemb; i++) {
		hk_pad_t *pad = ((hk_pad_t *) hc->pads.buf) + i;
		if (strcmp(pad->name, name) == 0) {
			return pad;
		}
	}

	return NULL;
}



void hk_class_prop_set(hk_class_t *hc, char *name, char *value)
{
	hk_prop_set(&hc->props, name, value);
}


char *hk_class_prop_get(hk_class_t *hc, char *name)
{
	return hk_prop_get(&hc->props, name);
}


void hk_class_prop_foreach(hk_class_t *hc, hk_prop_foreach_func func, char *user_data)
{
	hk_prop_foreach(&hc->props, func, user_data);
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
	hk_tab_init(&net->targets, sizeof(hk_net_target_t));

	return net;
}


void hk_net_connect(hk_net_t *net, hk_obj_t *obj, hk_pad_t *pad)
{
	hk_net_target_t *target;
	int i;

	/* Ensure pad is not bound to this net */
	for (i = 0; i < net->targets.nmemb; i++) {
		target = ((hk_net_target_t *) net->targets.buf) + i;
		if (target->pad == pad) {
			return;
		}
	}

	target = hk_tab_push(&net->targets);
	target->obj = obj;
	target->pad = pad;
}


/*
 * HAKit objects
 */

static HK_TAB_DECLARE(objs, hk_obj_t);


hk_obj_t *hk_obj_create(char *name, hk_class_t *class)
{
	hk_obj_t *obj = hk_tab_push(&objs);
	int size;

	obj->class = class;

	obj->name = strdup(name);

	size = sizeof(hk_net_t *) * class->pads.nmemb;
	obj->nets = (hk_net_t **) malloc(size);
	memset(obj->nets, 0, size);

	hk_prop_init(&obj->props);

	return obj;
}


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



void hk_obj_prop_set(hk_obj_t *obj, char *name, char *value)
{
	hk_prop_set(&obj->props, name, value);
}


char *hk_obj_prop_get(hk_obj_t *obj, char *name)
{
	char *value = hk_prop_get(&obj->props, name);

	if (value == NULL) {
		value = hk_class_prop_get(obj->class, name);
	}

	return value;
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
