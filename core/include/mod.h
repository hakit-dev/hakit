/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_MOD_H__
#define __HAKIT_MOD_H__

#include "buf.h"
#include "tab.h"
#include "prop.h"


typedef struct hk_pad_s hk_pad_t;
typedef struct hk_net_s hk_net_t;
typedef struct hk_obj_s hk_obj_t;
typedef struct hk_tile_s hk_tile_t;


/**
 * HAKit module class definition
 */

typedef struct {
	char *name;                   /**< Class name */
	char *version;                /**< Class version string */
	int (*new)(hk_obj_t *obj);                    /**< Class constructor */
	void (*start)(hk_obj_t *obj);                 /**< Start processing method */
	void (*input)(hk_pad_t *pad, char *value);    /**< Signal input method */
} hk_class_t;

extern int hk_class_register(hk_class_t *class);
extern hk_class_t *hk_class_find(char *name);


/**
 * HAKit module pads
 */

typedef enum {
	HK_PAD_IN=0,
	HK_PAD_OUT,
	HK_PAD_IO,
} hk_pad_dir_t;

struct hk_pad_s {
	hk_obj_t *obj;
	hk_pad_dir_t dir;
	char *name;
	buf_t value;
	hk_net_t *net;
	int lock;
	int state;
};

extern hk_pad_t *hk_pad_create(hk_obj_t *obj, hk_pad_dir_t dir, char *fmt, ...);
extern hk_pad_t *hk_pad_find(hk_obj_t *obj, char *name);

extern void hk_pad_update_str(hk_pad_t *pad, char *value);
extern void hk_pad_update_int(hk_pad_t *pad, int value);

extern char *hk_pad_get_value(hk_obj_t *obj, char *ref);

extern int hk_pad_is_connected(hk_pad_t *pad);

/**
 * HAKit nets
 */

struct hk_net_s {
	unsigned int id;
	hk_tab_t pads;  /**< Table of (hk_pad_t *) */
};

extern hk_net_t *hk_net_create(hk_tile_t *tile);
extern int hk_net_connect(hk_net_t *net, hk_pad_t *pad);


/**
 * HAKit objects
 */

struct hk_obj_s {
	char *name;
	hk_tile_t *tile;     /**< Tile object belongs to */
	hk_class_t *class;   /**< Class object is based on */
	hk_prop_t props;     /**< Object properties */
	hk_tab_t pads;       /**< Object pads : table of (hk_pad_t *) */
	void *ctx;           /**< Class-specific context */
};

extern hk_obj_t *hk_obj_create(hk_tile_t *tile, hk_class_t *class, char *name, int argc, char **argv);
extern hk_obj_t *hk_obj_find(hk_tile_t *tile, char *name);

extern void hk_obj_prop_set(hk_obj_t *obj, char *name, char *value);
extern char *hk_obj_prop_get(hk_obj_t *obj, char *name);
extern void hk_obj_prop_foreach(hk_obj_t *obj, hk_prop_foreach_func func, void *user_data);


/**
 * HAKit tiles
 */

struct hk_tile_s {
	char *dir;
	char *name;
	char *fname;
	hk_tab_t objs;       /**< Objects : table of (hk_obj_t *) */
	hk_tab_t nets;       /**< Nets : table of (hk_net_t *) */
};

typedef void (*hk_tile_foreach_func)(void *user_data, hk_tile_t *tile);

extern hk_tile_t *hk_tile_create(char *path);
extern void hk_tile_foreach(hk_tile_foreach_func func, void *user_data);
extern hk_tile_t *hk_tile_find(char *name);
extern void hk_tile_destroy(hk_tile_t *tile);
extern void hk_tile_start(hk_tile_t *tile);
extern char *hk_tile_rootdir(hk_tile_t *tile);
extern int hk_tile_nmemb(void);

#endif /* __HAKIT_MOD_H__ */
