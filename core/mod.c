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
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <libgen.h>

#include "log.h"
#include "buf.h"
#include "tab.h"
#include "files.h"
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


static void hk_pad_cleanup(hk_obj_t *obj)
{
	int i;

	for (i = 0; i < obj->pads.nmemb; i++) {
		hk_pad_t *pad = HK_TAB_VALUE(obj->pads, hk_pad_t *, i);
		free(pad->name);
		buf_cleanup(&pad->value);
		free(pad);
	}

	hk_tab_cleanup(&obj->pads);
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


char *hk_pad_get_value(hk_obj_t *obj, char *ref)
{
	char *pad_name = ref;
	char *value = NULL;

	// Fully qualified pad name expected: [[<app_name>.]<obj_name>.]<pad_name>
	char *pt2 = strrchr(ref, '.');
	if (pt2 != NULL) {
		hk_app_t *app = obj->app;
		char *obj_name = ref;

		*pt2 = '\0';
		pad_name = pt2+1;

		char *pt1 = strrchr(ref, '.');
		if (pt1 != NULL) {
			*pt1 = '\0';
			obj_name = pt1+1;
			app = hk_app_find(ref);
			*pt1 = '.';
		}

		if (app != NULL) {
			obj = hk_obj_find(app, obj_name);
		}
		else {
			obj = NULL;
		}

		*pt2 = '.';
	}

	if (obj != NULL) {
		hk_pad_t *pad = hk_pad_find(obj, pad_name);
		if (pad != NULL) {
			value = (char *) pad->value.base;
		}
	}

	return value;
}


/*
 * HAKit nets
 */

hk_net_t *hk_net_create(hk_app_t *app)
{
	hk_net_t *net = NULL;
	hk_net_t **pnet;
	int i;

	// Try to recycle a freed net entry
	for (i = 0; i < app->nets.nmemb; i++) {
		net = HK_TAB_VALUE(app->nets, hk_net_t *, i);
		if (net->id != 0) {
			net = NULL;
		}
		else {
			net->id = i+1;
			break;
		}
	}

	if (net == NULL) {
		net = (hk_net_t *) malloc(sizeof(hk_net_t));
		memset(net, 0, sizeof(hk_net_t));
		net->id = app->nets.nmemb+1;
		hk_tab_init(&net->pads, sizeof(hk_pad_t *));
		log_debug(2, "hk_net_create: new net #%d", net->id);
	}
	else {
		log_debug(2, "hk_net_create: recycled net #%d", net->id);
	}

	pnet = hk_tab_push(&app->nets);
	*pnet = net;

	return net;
}


static void hk_net_destroy(hk_net_t *net)
{
	net->id = 0;
	hk_tab_cleanup(&net->pads);
	free(net);
}


int hk_net_connect(hk_net_t *net, hk_pad_t *pad)
{
	hk_pad_t **ppad;

	log_debug(2, "hk_net_connect app=%s net=#%d pad=%s.%s", pad->obj->app->name, net->id, pad->obj->name, pad->name);

	/* Check pad is not already connected */
	if (pad->net != NULL) {
		log_str("ERROR: pad '%s.%s.%s' already connected to net #%d", pad->obj->app->name, pad->obj->name, pad->name, net->id);
		return 0;
	}

	ppad = hk_tab_push(&net->pads);
	*ppad = pad;
	pad->net = net;

	return 1;
}


static void hk_net_merge(hk_net_t *net1, hk_net_t *net2)
{
	int i;

	log_debug(2, "hk_net_merge net1=#%d net2=#%d", net1->id, net2->id);

	if (net1 == net2) {
		return;
	}

	for (i = 0; i < net2->pads.nmemb; i++) {
		hk_pad_t *pad2 = HK_TAB_VALUE(net2->pads, hk_pad_t *, i);
		pad2->net = NULL;
		hk_net_connect(net1, pad2);
	}

	net2->id = 0;
	hk_tab_cleanup(&net2->pads);
}


/*
 * HAKit objects
 */

hk_obj_t *hk_obj_find(hk_app_t *app, char *name)
{
	int i;

	for (i = 0; i < app->objs.nmemb; i++) {
		hk_obj_t *obj = HK_TAB_VALUE(app->objs, hk_obj_t *, i);
		if (strcmp(obj->name, name) == 0) {
			return obj;
		}
	}

	return NULL;
}


hk_obj_t *hk_obj_create(hk_app_t *app, hk_class_t *class, char *name, int argc, char **argv)
{
	hk_obj_t *obj;
	hk_obj_t **pobj;
	int i;

	log_debug(2, "hk_obj_create app='%s' class='%s' name='%s'", app->name, class->name, name);

	obj = hk_obj_find(app, name);
	if (obj != NULL) {
		log_str("ERROR: Object %s.%s already exists", app->name, name);
		return NULL;
	}

	obj = (hk_obj_t *) malloc(sizeof(hk_obj_t));
	obj->name = strdup(name);
	obj->app = app;
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

	pobj = hk_tab_push(&app->objs);
	*pobj = obj;

	return obj;
}


static void hk_obj_destroy(hk_obj_t *obj)
{
	hk_prop_cleanup(&obj->props);
	hk_pad_cleanup(obj);
	free(obj->name);
	free(obj);
}


static int hk_obj_net(hk_pad_t *pad1, char *ref)
{
	hk_obj_t *obj1 =  pad1->obj;
	hk_app_t *app = obj1->app;
	char *pt;
	hk_obj_t *obj2;
	hk_pad_t *pad2;
	int ret = 0;

	log_debug(2, "hk_obj_net %s.%s.%s=%s", app->name, obj1->name, pad1->name, ref);

	// Parse target pad
	pt = strrchr(ref, '.');
	if (pt == NULL) {
		log_str("ERROR: %s.%s.%s: Syntax error in target pad specification '%s'", app->name, obj1->name, pad1->name, ref);
		return 0;
	}

	*pt = '\0';

	obj2 = hk_obj_find(app, ref);
	if (obj2 == NULL) {
		log_str("ERROR: %s.%s:%s: Referencing undefined object '%s'", app->name, obj1->name, pad1->name, ref);
		goto done;
	}

	pad2 = hk_pad_find(obj2, pt+1);
	if (pad2 == NULL) {
		log_str("ERROR: %s.%s:%s: Referencing unknown pad '%s' in object '%s'", app->name, obj1->name, pad1->name, pt+1, obj2->name);
		goto done;
	}

	if (pad1->net != NULL) {
		if (pad2->net != NULL) {
			hk_net_merge(pad1->net, pad2->net);
		}
		else {
			hk_net_connect(pad1->net, pad2);
		}
	}
	else {
		if (pad2->net != NULL) {
			hk_net_connect(pad2->net, pad1);
		}
		else {
			hk_net_t *net = hk_net_create(app);
			hk_net_connect(net, pad1);
			hk_net_connect(net, pad2);
		}
	}

	ret = 1;
done:
	*pt = '.';
	return ret;
}


static void hk_obj_preset(hk_pad_t *pad, char *value)
{
	log_debug(2, "hk_obj_preset %s.%s.%s='%s'", pad->obj->app->name, pad->obj->name, pad->name, value);

	if (pad->dir == HK_PAD_OUT) {
		hk_pad_update_str(pad, value);
	}
	else {
		buf_set_str(&pad->value, value);
		hk_pad_update_input(pad, value);
	}
}


static int hk_obj_setup(hk_obj_t *obj, char *name, char *value)
{
	hk_pad_t *pad = hk_pad_find(obj, name);

	if ((pad != NULL) && (value != NULL)) {
		if (*value == '$') {
			hk_obj_net(pad, value+1);
		}
		else {
			hk_obj_preset(pad, value);
		}
	}

	return 1;
}


void hk_obj_prop_set(hk_obj_t *obj, char *name, char *value)
{
	log_debug(2, "hk_obj_prop_set obj='%s.%s': %s='%s'", obj->app->name, obj->name, name, value);
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


/**
 * HAKit applications
 */

static HK_TAB_DECLARE(apps, hk_app_t *);

#define HK_APP_ENTRY(i) HK_TAB_VALUE(apps, hk_app_t *, i)


hk_app_t *hk_app_find(char *name)
{
	int i;

	/* Remove app from list */
	for (i = 0; i < apps.nmemb; i++) {
		hk_app_t *app = HK_APP_ENTRY(i);
		if (strcmp(app->name, name) == 0) {
			return app;
		}
	}

	return NULL;
}


hk_app_t *hk_app_create(char *path)
{
	hk_app_t *app = malloc(sizeof(hk_app_t));
	memset(app, 0, sizeof(hk_app_t));

	log_debug(2, "hk_app_register '%s'", path);

	// Setup name, directory and app file
	if (is_dir(path)) {
		app->dir = realpath(path, NULL);

		char *str = strdup(path);
		app->name = strdup(basename(str));
		free(str);

		int size = strlen(path) + 8;
		app->fname = malloc(size);
		snprintf(app->fname, size, "%s/app.hk", path);
	}
	else if (is_file(path)) {
		char *str = strdup(path);
		app->dir = realpath(dirname(str), NULL);
		free(str);

		str = strdup(path);
		app->name = strdup(basename(str));
		free(str);
		char *dot = strrchr(app->name, '.');
		if ((dot != NULL) && (strcmp(dot, ".hk") == 0)) {
			*dot = '\0';
		}

		app->fname = strdup(path);
	}
	else {
		log_str("ERROR: %s: No such file or directory", path);
		free(app);
		return NULL;
	}

	log_debug(2, "  => dir='%s' name='%s' fname='%s'", app->dir, app->name, app->fname);

	/* Check for name conflict */
	if (hk_app_find(app->name) != NULL) {
		log_str("ERROR: Application '%s' loaded more than once", app->name);
		hk_app_destroy(app);
		return NULL;
	}

	/* Init object and net tables */
	hk_tab_init(&app->objs, sizeof(hk_obj_t *));
	hk_tab_init(&app->nets, sizeof(hk_net_t *));

	/* Add new entry to app table */
	hk_app_t **papp = hk_tab_push(&apps);
	*papp = app;

	return app;
}


void hk_app_destroy(hk_app_t *app)
{
	int i;

	/* Remove app from list */
	for (i = 0; i < apps.nmemb; i++) {
		hk_app_t **papp = HK_TAB_PTR(apps, hk_app_t *, i);
		if (*papp == app) {
			*papp = NULL;
			break;
		}
	}

	/* Destroy objects and nets */
	for (i = 0; i < app->nets.nmemb; i++) {
		hk_net_t *net = HK_TAB_VALUE(app->objs, hk_net_t *, i);
		hk_net_destroy(net);
	}
	hk_tab_cleanup(&app->nets);

	for (i = 0; i < app->objs.nmemb; i++) {
		hk_obj_t *obj = HK_TAB_VALUE(app->objs, hk_obj_t *, i);
		hk_obj_destroy(obj);
	}
	hk_tab_cleanup(&app->objs);

	/* Free descriptor content */
	free(app->dir);
	free(app->name);
	free(app->fname);

	/* Free descriptor */
	memset(app, 0, sizeof(hk_app_t));  // Defencive operation to prevent from referencing unallocated pointers
	free(app);
}


void hk_app_start(hk_app_t *app)
{
	int i;

	/* Create nets and presets */
	for (i = 0; i < app->objs.nmemb; i++) {
		hk_obj_t *obj = HK_TAB_VALUE(app->objs, hk_obj_t *, i);
		hk_prop_foreach(&obj->props, (hk_prop_foreach_func) hk_obj_setup, (void *) obj);
	}

	/* Invoke start handlers */
	for (i = 0; i < app->objs.nmemb; i++) {
		hk_obj_t *obj = HK_TAB_VALUE(app->objs, hk_obj_t *, i);

		if (obj->class->start != NULL) {
			log_debug(2, "Starting object '%s'", obj->name);
			obj->class->start(obj);
		}
	}
}
