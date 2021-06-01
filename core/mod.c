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


int hk_class_register(hk_class_t *class)
{
	hk_class_t **pclass;

	if (hk_class_find(class->name) != NULL) {
		log_str("ERROR: Class '%s' already exists", class->name);
		return -1;
	}

	pclass = hk_tab_push(&classes);
	*pclass = class;

        return 0;
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

	// Fully qualified pad name expected: [[<tile_name>.]<obj_name>.]<pad_name>
	char *pt2 = strrchr(ref, '.');
	if (pt2 != NULL) {
		hk_tile_t *tile = obj->tile;
		char *obj_name = ref;

		*pt2 = '\0';
		pad_name = pt2+1;

		char *pt1 = strrchr(ref, '.');
		if (pt1 != NULL) {
			*pt1 = '\0';
			obj_name = pt1+1;
			tile = hk_tile_find(ref);
			*pt1 = '.';
		}

		if (tile != NULL) {
			obj = hk_obj_find(tile, obj_name);
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


int hk_pad_is_connected(hk_pad_t *pad)
{
        return (pad->net != NULL);
}


/*
 * HAKit nets
 */

hk_net_t *hk_net_create(hk_tile_t *tile)
{
	hk_net_t *net = NULL;
	hk_net_t **pnet;
	int i;

	// Try to recycle a freed net entry
	for (i = 0; i < tile->nets.nmemb; i++) {
		net = HK_TAB_VALUE(tile->nets, hk_net_t *, i);
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
		net->id = tile->nets.nmemb+1;
		hk_tab_init(&net->pads, sizeof(hk_pad_t *));
		log_debug(2, "hk_net_create: new net #%d", net->id);
	}
	else {
		log_debug(2, "hk_net_create: recycled net #%d", net->id);
	}

	pnet = hk_tab_push(&tile->nets);
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

	log_debug(2, "hk_net_connect tile=%s net=#%d pad=%s.%s", pad->obj->tile->name, net->id, pad->obj->name, pad->name);

	/* Check pad is not already connected */
	if (pad->net != NULL) {
		log_str("ERROR: pad '%s.%s.%s' already connected to net #%d", pad->obj->tile->name, pad->obj->name, pad->name, net->id);
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

hk_obj_t *hk_obj_find(hk_tile_t *tile, char *name)
{
	int i;

	for (i = 0; i < tile->objs.nmemb; i++) {
		hk_obj_t *obj = HK_TAB_VALUE(tile->objs, hk_obj_t *, i);
		if (strcmp(obj->name, name) == 0) {
			return obj;
		}
	}

	return NULL;
}


hk_obj_t *hk_obj_create(hk_tile_t *tile, hk_class_t *class, char *name, int argc, char **argv)
{
	hk_obj_t *obj;
	hk_obj_t **pobj;
	int i;

	log_debug(2, "hk_obj_create tile='%s' class='%s' name='%s'", tile->name, class->name, name);

	obj = hk_obj_find(tile, name);
	if (obj != NULL) {
		log_str("ERROR: Object %s.%s already exists", tile->name, name);
		return NULL;
	}

	obj = (hk_obj_t *) malloc(sizeof(hk_obj_t));
	obj->name = strdup(name);
	obj->tile = tile;
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

	pobj = hk_tab_push(&tile->objs);
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
	hk_tile_t *tile = obj1->tile;
	char *pt;
	hk_obj_t *obj2;
	hk_pad_t *pad2;
	int ret = 0;

	log_debug(2, "hk_obj_net %s.%s.%s=%s", tile->name, obj1->name, pad1->name, ref);

	// Parse target pad
	pt = strrchr(ref, '.');
	if (pt == NULL) {
		log_str("ERROR: %s.%s.%s: Syntax error in target pad specification '%s'", tile->name, obj1->name, pad1->name, ref);
		return 0;
	}

	*pt = '\0';

	obj2 = hk_obj_find(tile, ref);
	if (obj2 == NULL) {
		log_str("ERROR: %s.%s:%s: Referencing undefined object '%s'", tile->name, obj1->name, pad1->name, ref);
		goto done;
	}

	pad2 = hk_pad_find(obj2, pt+1);
	if (pad2 == NULL) {
		log_str("ERROR: %s.%s:%s: Referencing unknown pad '%s' in object '%s'", tile->name, obj1->name, pad1->name, pt+1, obj2->name);
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
			hk_net_t *net = hk_net_create(tile);
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
	log_debug(2, "hk_obj_preset %s.%s.%s='%s'", pad->obj->tile->name, pad->obj->name, pad->name, value);

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
	log_debug(2, "hk_obj_prop_set obj='%s.%s': %s='%s'", obj->tile->name, obj->name, name, value);
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
 * HAKit tiles
 */

static HK_TAB_DECLARE(tiles, hk_tile_t *);

#define HK_TILE_ENTRY(i) HK_TAB_VALUE(tiles, hk_tile_t *, i)


int hk_tile_nmemb(void)
{
        return tiles.nmemb;
}


void hk_tile_foreach(hk_tile_foreach_func func, void *user_data)
{
	int i;

	/* Remove tile from list */
	for (i = 0; i < tiles.nmemb; i++) {
		hk_tile_t *tile = HK_TILE_ENTRY(i);
                func(user_data, tile);
	}
}


hk_tile_t *hk_tile_find(char *name)
{
	int i;

	/* Remove tile from list */
	for (i = 0; i < tiles.nmemb; i++) {
		hk_tile_t *tile = HK_TILE_ENTRY(i);
		if (strcmp(tile->name, name) == 0) {
			return tile;
		}
	}

	return NULL;
}


hk_tile_t *hk_tile_create(char *path)
{
	hk_tile_t *tile = malloc(sizeof(hk_tile_t));
	memset(tile, 0, sizeof(hk_tile_t));

	log_debug(2, "hk_tile_register '%s'", path);

	// Setup name, directory and tile file
	if (is_dir(path)) {
		tile->dir = realpath(path, NULL);

		char *str = strdup(path);
		tile->name = strdup(basename(str));
		free(str);

		int size = strlen(path) + 10;
		tile->fname = malloc(size);
		snprintf(tile->fname, size, "%s/tile.hk", path);
	}
	else if (is_file(path)) {
		char *str = strdup(path);
		tile->dir = realpath(dirname(str), NULL);
		free(str);

		str = strdup(path);
		tile->name = strdup(basename(str));
		free(str);
		char *dot = strrchr(tile->name, '.');
		if ((dot != NULL) && (strcmp(dot, ".hk") == 0)) {
			*dot = '\0';
		}

		tile->fname = strdup(path);
	}
	else {
		log_str("ERROR: %s: No such file or directory", path);
		free(tile);
		return NULL;
	}

	log_debug(2, "  => dir='%s' name='%s' fname='%s'", tile->dir, tile->name, tile->fname);

	/* Check for name conflict */
	if (hk_tile_find(tile->name) != NULL) {
		log_str("ERROR: Tile '%s' loaded more than once", tile->name);
		hk_tile_destroy(tile);
		return NULL;
	}

	/* Init object and net tables */
	hk_tab_init(&tile->objs, sizeof(hk_obj_t *));
	hk_tab_init(&tile->nets, sizeof(hk_net_t *));

	/* Add new entry to tile table */
	hk_tile_t **ptile = hk_tab_push(&tiles);
	*ptile = tile;

	return tile;
}


void hk_tile_destroy(hk_tile_t *tile)
{
	int i;

	/* Remove tile from list */
	for (i = 0; i < tiles.nmemb; i++) {
		hk_tile_t **ptile = HK_TAB_PTR(tiles, hk_tile_t *, i);
		if (*ptile == tile) {
			*ptile = NULL;
			break;
		}
	}

	/* Destroy objects and nets */
	for (i = 0; i < tile->nets.nmemb; i++) {
		hk_net_t *net = HK_TAB_VALUE(tile->objs, hk_net_t *, i);
		hk_net_destroy(net);
	}
	hk_tab_cleanup(&tile->nets);

	for (i = 0; i < tile->objs.nmemb; i++) {
		hk_obj_t *obj = HK_TAB_VALUE(tile->objs, hk_obj_t *, i);
		hk_obj_destroy(obj);
	}
	hk_tab_cleanup(&tile->objs);

	/* Free descriptor content */
	free(tile->dir);
	free(tile->name);
	free(tile->fname);

	/* Free descriptor */
	memset(tile, 0, sizeof(hk_tile_t));  // Defensive operation to prevent from referencing unallocated pointers
	free(tile);
}


void hk_tile_start(hk_tile_t *tile)
{
	int i;

	/* Create nets and presets */
	for (i = 0; i < tile->objs.nmemb; i++) {
		hk_obj_t *obj = HK_TAB_VALUE(tile->objs, hk_obj_t *, i);
		hk_prop_foreach(&obj->props, (hk_prop_foreach_func) hk_obj_setup, (void *) obj);
	}

	/* Invoke start handlers */
	for (i = 0; i < tile->objs.nmemb; i++) {
		hk_obj_t *obj = HK_TAB_VALUE(tile->objs, hk_obj_t *, i);

		if (obj->class->start != NULL) {
			log_debug(2, "Starting object '%s'", obj->name);
			obj->class->start(obj);
		}
	}
}


char *hk_tile_rootdir(hk_tile_t *tile)
{
        return dirname(strdup(tile->dir));
}
