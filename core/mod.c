#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "log.h"

#include "mod.h"


/*
 * HAKit properties collection
 */

static hk_prop_entry_t *hk_prop_find(hk_prop_t *prop, char *name)
{
	int i;

	for (i = 0; i < prop->n; i++) {
		hk_prop_entry_t *entry = &prop->tab[i];
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
		int i = prop->n;
		prop->n++;
		prop->tab = (hk_prop_entry_t *) realloc(prop->tab, sizeof(hk_prop_entry_t) * prop->n);

		entry = &prop->tab[i];
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

	for (i = 0; i < prop->n; i++) {
		hk_prop_entry_t *entry = &prop->tab[i];
		if (!func(user_data, entry->name, entry->value)) {
			return;
		}
	}
}


/*
 * HAKit module class definition
 */

static hk_class_t *classes = NULL;
static int nclasses = 0;


hk_class_t *hk_class_find(char *name)
{
	int i;

	for (i = 0; i < nclasses; i++) {
		hk_class_t *hc = &classes[i];
		if (strcmp(hc->name, name) == 0) {
			return hc;
		}
	}

	return NULL;
}


hk_class_t *hk_class_create(char *name)
{
	hk_class_t *hc = hk_class_find(name);
	int i;

	if (hc != NULL) {
		log_str("ERROR: Class '%s' created more than once", name);
		return NULL;
	}

	i = nclasses;
	nclasses++;
	classes = (hk_class_t *) realloc(classes, sizeof(hk_class_t) * nclasses);

	hc = &classes[i];
	memset(hc, 0, sizeof(hk_class_t));

	hc->name = strdup(name);

	return hc;
}

hk_pad_t *hk_class_pad_add(hk_class_t *hc, char *name, hk_pad_input_func func)
{
	hk_pad_t *pad;
	int i;

	i = hc->npads;
	hc->npads++;
	hc->pads = (hk_pad_t *) realloc(hc->pads, sizeof(hk_pad_t) * hc->npads);

	pad = &hc->pads[i];
	pad->name = strdup(name);
	pad->func = func;

	return pad;
}


void hk_class_pad_foreach(hk_class_t *hc, hk_class_pad_foreach_func func, void *user_data)
{
	int i;

	for (i = 0; i < hc->npads; i++) {
		hk_pad_t *pad = &hc->pads[i];
		if (!func(user_data, pad)) {
			return;
		}
	}
}
