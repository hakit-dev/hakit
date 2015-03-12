#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "log.h"
#include "mod.h"


/*
 * HAKit properties collection
 */

static void hk_prop_init(hk_prop_t *prop)
{
	hk_tab_init(&prop->tab, sizeof(hk_prop_entry_t));
}


static hk_prop_entry_t *hk_prop_find(hk_prop_t *prop, char *name)
{
	int i;

	for (i = 0; i < prop->tab.nmemb; i++) {
		hk_prop_entry_t *entry = ((hk_prop_entry_t *) &prop->tab.buf) + i;
		if (strcmp(entry->name, name) == 0) {
			return entry;
		}
	}

	return NULL;
}


void hk_prop_set(hk_prop_t *prop, char *name, char *value)
{
	hk_prop_entry_t *entry = hk_prop_find(prop, name);

	if (entry == NULL) {
		entry = hk_tab_push(&prop->tab);
		entry->name = strdup(name);
	}
	else {
		if (entry->value != NULL) {
			free(entry->value);
		}
	}

	entry->value = strdup(value);
}


char *hk_prop_get(hk_prop_t *prop, char *name)
{
	hk_prop_entry_t *entry = hk_prop_find(prop, name);

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


/*
 * HAKit module class definition
 */

HK_TAB_DECLARE(classes, hk_class_t);


hk_class_t *hk_class_find(char *name)
{
	int i;

	for (i = 0; i < classes.nmemb; i++) {
		hk_class_t *hc = ((hk_class_t *) classes.buf) + i;
		if (strcmp(hc->name, name) == 0) {
			return hc;
		}
	}

	return NULL;
}


hk_class_t *hk_class_create(char *name)
{
	hk_class_t *hc = hk_class_find(name);

	if (hc != NULL) {
		log_str("ERROR: Class '%s' created more than once", name);
		return NULL;
	}

	hc = hk_tab_push(&classes);

	hc->name = strdup(name);
	hk_prop_init(&hc->prop);
	hk_tab_init(&hc->pads, sizeof(hk_pad_t));

	return hc;
}


hk_pad_t *hk_class_pad_add(hk_class_t *hc, char *name, hk_pad_input_func func)
{
	hk_pad_t *pad = hk_tab_push(&hc->pads);
	pad->name = strdup(name);
	pad->func = func;

	return pad;
}


void hk_class_pad_foreach(hk_class_t *hc, hk_class_pad_foreach_func func, void *user_data)
{
	hk_tab_foreach(&hc->pads, (hk_tab_foreach_func) func, user_data);
}


void hk_class_prop_set(hk_class_t *hc, char *name, char *value)
{
	hk_prop_set(&hc->prop, name, value);
}


char *hk_class_prop_get(hk_class_t *hc, char *name)
{
	return hk_prop_get(&hc->prop, name);
}


void hk_class_prop_foreach(hk_class_t *hc, hk_prop_foreach_func func, char *user_data)
{
	hk_prop_foreach(&hc->prop, func, user_data);
}
