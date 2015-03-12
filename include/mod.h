#ifndef __HAKIT_MOD_H__
#define __HAKIT_MOD_H__

#include "tab.h"

/*
 * HAKit properties collection
 */

typedef struct {
	char *name;
	char *value;
} hk_prop_entry_t;

typedef struct {
	hk_tab_t tab;
} hk_prop_t;

typedef int (*hk_prop_foreach_func)(void *user_data, char *name, char *value);

extern void hk_prop_set(hk_prop_t *prop, char *name, char *value);
extern char *hk_prop_get(hk_prop_t *prop, char *name);
extern void hk_prop_foreach(hk_prop_t *prop, hk_prop_foreach_func func, char *user_data);

/*
 * HAKit module class definition
 */

typedef struct hk_pad_s hk_pad_t;
typedef struct hk_obj_s hk_obj_t;

typedef void (*hk_pad_input_func)(hk_obj_t *obj, char *value);
typedef int (*hk_class_pad_foreach_func)(void *user_data, hk_pad_t *pad);

struct hk_pad_s {
	char *name;
	hk_pad_input_func func;
};

typedef struct {
	char *name;
	hk_prop_t prop;
	hk_tab_t pads;
} hk_class_t;


extern hk_class_t *hk_class_create(char *name);
extern hk_pad_t *hk_class_pad_add(hk_class_t *hc, char *name, hk_pad_input_func func);
extern void hk_class_pad_foreach(hk_class_t *hc, hk_class_pad_foreach_func func, void *user_data);

extern void hk_class_prop_set(hk_class_t *hc, char *name, char *value);
extern char *hk_class_prop_get(hk_class_t *hc, char *name);
extern void hk_class_prop_foreach(hk_class_t *hc, hk_prop_foreach_func func, char *user_data);


/*
 * HAKit nets
 */

typedef struct {
	hk_obj_t *obj;
	hk_pad_t *pad;
} hk_net_target_t;

typedef struct {
	char *name;
	hk_net_target_t *targets;
	int ntargets;
} hk_net_t;

extern int hk_net_create(char *name);
extern int hk_net_connect(char *name, hk_obj_t *obj, hk_pad_t *pad);


/*
 * HAKit objects
 */

struct hk_obj_s {
	hk_class_t *class;   /* Class object is based on */
	hk_net_t *nets;      /* One net per class pad */
	hk_prop_t prop;   /* Properties overloaded from class properties */
};

extern hk_obj_t *hk_obj_create(hk_class_t *class);
extern int hk_obj_connect(hk_obj_t *obj, char *pad_name, char *net_name);

#endif /* __HAKIT_MOD_H__ */
