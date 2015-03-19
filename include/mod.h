#ifndef __HAKIT_MOD_H__
#define __HAKIT_MOD_H__

#include "tab.h"
#include "prop.h"


/*
 * HAKit module class definition
 */

typedef struct hk_pad_s hk_pad_t;
typedef struct hk_obj_s hk_obj_t;

typedef void (*hk_pad_input_func)(hk_obj_t *obj, char *value);
typedef int (*hk_class_pad_foreach_func)(void *user_data, hk_pad_t *pad);

struct hk_pad_s {
	int id;
	char *name;
	hk_pad_input_func func;
};

typedef struct {
	char *name;
	hk_prop_t props;
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
	hk_tab_t targets;
} hk_net_t;

extern hk_net_t *hk_net_create(char *name);
extern hk_net_t *hk_net_find(char *name);
extern void hk_net_connect(hk_net_t *net, hk_obj_t *obj, hk_pad_t *pad);


/*
 * HAKit objects
 */

struct hk_obj_s {
	char *name;
	hk_class_t *class;   /* Class object is based on */
	hk_net_t **nets;      /* One net per class pad */
	hk_prop_t props;     /* Properties overloaded from class properties */
};

extern hk_obj_t *hk_obj_create(char *name, hk_class_t *class);
extern int hk_obj_connect(hk_obj_t *obj, char *pad_name, char *net_name);

extern void hk_obj_prop_set(hk_obj_t *obj, char *name, char *value);
extern char *hk_obj_prop_get(hk_obj_t *obj, char *name);
extern void hk_obj_prop_foreach(hk_obj_t *obj, hk_prop_foreach_func func, char *user_data);

#endif /* __HAKIT_MOD_H__ */
