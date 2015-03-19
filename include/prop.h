#ifndef __HAKIT_PROP_H__
#define __HAKIT_PROP_H__

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

extern void hk_prop_init(hk_prop_t *props);
extern void hk_prop_set(hk_prop_t *props, char *name, char *value);
extern char *hk_prop_get(hk_prop_t *props, char *name);
extern void hk_prop_foreach(hk_prop_t *props, hk_prop_foreach_func func, char *user_data);

#endif /* __HAKIT_PROP_H__ */
