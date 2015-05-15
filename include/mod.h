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

#include "tab.h"
#include "prop.h"


/**
 * HAKit module class definition
 */

typedef struct hk_pad_s hk_pad_t;
typedef struct hk_net_s hk_net_t;
typedef struct hk_obj_s hk_obj_t;

typedef hk_obj_t * (*hk_class_new_func)(hk_obj_t *obj);
typedef void (*hk_class_input_func)(hk_pad_t *pad, char *value);

typedef struct {
	char *name;                   /**< Class name */
	char *version;                /**< Class version string */
	hk_class_new_func new;        /**< Class constructor */
	hk_class_input_func input;    /**< Signal input method */
} hk_class_t;

extern void hk_class_register(hk_class_t *class);
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
	hk_net_t *net;
	int lock;
	int state;
};

extern hk_pad_t *hk_pad_create(hk_obj_t *obj, hk_pad_dir_t dir, char *fmt, ...);
extern hk_pad_t *hk_pad_find(hk_obj_t *obj, char *name);

extern void hk_pad_update_str(hk_pad_t *pad, char *value);
extern void hk_pad_update_int(hk_pad_t *pad, int value);


/**
 * HAKit nets
 */

struct hk_net_s {
	char *name;
	hk_tab_t ppads;  /**< Table of (hk_pad_t *) */
};

extern hk_net_t *hk_net_create(char *name);
extern hk_net_t *hk_net_find(char *name);
extern int hk_net_connect(hk_net_t *net, hk_pad_t *pad);


/**
 * HAKit objects
 */

struct hk_obj_s {
	char *name;
	hk_class_t *class;   /**< Class object is based on */
	hk_prop_t props;     /**< Object properties */
	hk_tab_t pads;       /**< Object pads : table of (hk_pad_t) */
	void *ctx;           /**< Class-specific context */
};

extern hk_obj_t *hk_obj_create(char *name, hk_class_t *class);

extern void hk_obj_prop_set(hk_obj_t *obj, char *name, char *value);
extern char *hk_obj_prop_get(hk_obj_t *obj, char *name);
extern void hk_obj_prop_foreach(hk_obj_t *obj, hk_prop_foreach_func func, char *user_data);

#endif /* __HAKIT_MOD_H__ */
