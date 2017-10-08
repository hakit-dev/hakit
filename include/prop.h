/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

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
extern int hk_prop_get_int(hk_prop_t *props, char *name);
extern void hk_prop_foreach(hk_prop_t *props, hk_prop_foreach_func func, char *user_data);
extern void hk_prop_cleanup(hk_prop_t *props);

#endif /* __HAKIT_PROP_H__ */
