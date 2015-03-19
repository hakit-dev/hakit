#include <stdio.h>
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
		hk_prop_entry_t *entry = ((hk_prop_entry_t *) &props->tab.buf) + i;
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


void hk_prop_foreach(hk_prop_t *prop, hk_prop_foreach_func func, char *user_data)
{
	int i;

	for (i = 0; i < prop->tab.nmemb; i++) {
		hk_prop_entry_t *entry = ((hk_prop_entry_t *) &prop->tab.buf) + i;
		if (!func(user_data, entry->name, entry->value)) {
			return;
		}
	}
}
