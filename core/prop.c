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
#include <string.h>
#include <malloc.h>

#include "log.h"
#include "prop.h"


void hk_prop_init(hk_prop_t *props)
{
	hk_tab_init(&props->tab, sizeof(hk_prop_entry_t));
}


static hk_prop_entry_t *hk_prop_find(hk_prop_t *props, char *name)
{
	int i;

	for (i = 0; i < props->tab.nmemb; i++) {
		hk_prop_entry_t *entry = HK_TAB_PTR(props->tab, hk_prop_entry_t, i);
		if (strcmp(entry->name, name) == 0) {
			return entry;
		}
	}

	return NULL;
}


void hk_prop_set(hk_prop_t *props, char *name, char *value)
{
	hk_prop_entry_t *entry = hk_prop_find(props, name);

	if (entry == NULL) {
		entry = hk_tab_push(&props->tab);
		entry->name = strdup(name);
	}
	else {
		if (entry->value != NULL) {
			free(entry->value);
			entry->value = NULL;
		}
	}

	entry->value = strdup(value);
}


char *hk_prop_get(hk_prop_t *props, char *name)
{
	hk_prop_entry_t *entry = hk_prop_find(props, name);

	if (entry == NULL) {
		return NULL;
	}

	return entry->value;
}


int hk_prop_get_int(hk_prop_t *props, char *name)
{
	char *s = hk_prop_get(props, name);
	if (s == NULL) {
		return 0;
	}

	return strtol(s, NULL, 0);
}


void hk_prop_foreach(hk_prop_t *props, hk_prop_foreach_func func, char *user_data)
{
	int i;

	for (i = 0; i < props->tab.nmemb; i++) {
		hk_prop_entry_t *entry = HK_TAB_PTR(props->tab, hk_prop_entry_t, i);
		if (!func(user_data, entry->name, entry->value)) {
			return;
		}
	}
}


void hk_prop_cleanup(hk_prop_t *props)
{
	int i;

	for (i = 0; i < props->tab.nmemb; i++) {
		hk_prop_entry_t *entry = HK_TAB_PTR(props->tab, hk_prop_entry_t, i);

		free(entry->name);
		if (entry->value != NULL) {
			free(entry->value);
		}
	}

	hk_tab_cleanup(&props->tab);
}
