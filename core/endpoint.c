/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2017 Sylvain Giroudon
 *
 * HAKit Endpoints management
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "tab.h"
#include "log.h"
#include "endpoint.h"


/*
 * Generic endpoint operations
 */

static inline const char *hk_ep_type_str(hk_ep_t *ep)
{
	static const char *tab[HK_EP_NTYPES] = {
		"sink", "source"
	};

	if (ep->type >= HK_EP_NTYPES) {
		return "?";
	}

	return tab[ep->type];
}


static void hk_ep_set_widget(hk_ep_t *ep, char *widget_name)
{
	if (ep->obj != NULL) {
		log_debug(2, "hk_ep_set_widget name='%s' widget='%s'", ep->obj->name, widget_name);
		if (ep->widget != NULL) {
			free(ep->widget);
			ep->widget = NULL;
		}
		if (widget_name != NULL) {
			ep->widget = strdup(widget_name);
		}
	}
	else {
		log_str("PANIC: Attempting to set widget name on dead %s #%d\n", hk_ep_type_str(ep), ep->id);
	}
}


void hk_ep_append_name(hk_ep_t *ep, buf_t *out_buf)
{
	buf_append_str(out_buf, ep->obj->name);
}


void hk_ep_append_value(hk_ep_t *ep, buf_t *out_buf)
{
        buf_append(out_buf, ep->value.base, ep->value.len);
}


static int hk_ep_id(hk_ep_t *ep)
{
        if (ep == NULL) {
                return -1;
        }
        if (ep->obj == NULL) {
                return -1;
        }

        return ep->id;
}


static int hk_ep_is_public(hk_ep_t *ep)
{
        if (ep == NULL) {
                return 0;
        }
        if (ep->obj == NULL) {
                return 0;
        }

	return (ep->flag & HK_FLAG_LOCAL) ? 0:1;
}


void hk_ep_dump(hk_ep_t *ep, buf_t *out_buf)
{
        if (ep == NULL) {
                return;
        }
        if (ep->obj == NULL) {
                return;
        }

	buf_append_str(out_buf, (char *) hk_ep_type_str(ep));
	buf_append_byte(out_buf, ' ');
	buf_append_str(out_buf, ep->widget);
        buf_append_byte(out_buf, ' ');
        if (hk_tile_nmemb() > 1) {
                buf_append_str(out_buf, ep->obj->tile->name);
                buf_append_byte(out_buf, '.');
        }
	buf_append_str(out_buf, ep->obj->name);
	buf_append_byte(out_buf, ' ');
	buf_append(out_buf, ep->value.base, ep->value.len);
	buf_append_byte(out_buf, '\n');
}


static void hk_ep_cleanup(hk_ep_t *ep)
{
        buf_cleanup(&ep->value);

        if (ep->widget != NULL) {
                free(ep->widget);
        }

        memset(ep, 0, sizeof(hk_ep_t));
}


/*
 * Sinks
 */

hk_sink_t *hk_sink_retrieve_by_name(hk_endpoints_t *eps, char *name)
{
	int i;

	for (i = 0; i < eps->sinks.nmemb; i++) {
		hk_sink_t *sink = HK_TAB_VALUE(eps->sinks, hk_sink_t *, i);
                if ((sink != NULL) && (sink->ep.obj != NULL) && (sink->ep.obj->name != NULL)) {
			if (strcmp(sink->ep.obj->name, name) == 0) {
				return sink;
			}
		}
	}

	return NULL;
}


hk_sink_t *hk_sink_retrieve_by_id(hk_endpoints_t *eps, int id)
{
	if (id < 0) {
		return NULL;
	}

	if (id >= eps->sinks.nmemb) {
		return NULL;
	}
	
	hk_sink_t *sink = HK_TAB_VALUE(eps->sinks, hk_sink_t *, id);

	if (sink->ep.obj == NULL) {
		return NULL;
	}

	return sink;
}


static hk_sink_t *hk_sink_alloc(hk_endpoints_t *eps, hk_obj_t *obj, int local)
{
	hk_sink_t *sink = NULL;
	int i;

        /* Search a free entry in the sink table */
	for (i = 0; (i < eps->sinks.nmemb) && (sink == NULL); i++) {
		sink = HK_TAB_VALUE(eps->sinks, hk_sink_t *, i);
		if (sink->ep.obj != NULL) {
                        sink = NULL;
		}
	}

        /* If no free entry found, create a new one */
        if (sink == NULL) {
                hk_sink_t **psink = hk_tab_push(&eps->sinks);
                *psink = sink = (hk_sink_t *) malloc(sizeof(hk_sink_t));
                memset(sink, 0, sizeof(hk_sink_t));
        }

	sink->ep.type = HK_EP_SINK;
	sink->ep.id = i;
	sink->ep.obj = obj;
	buf_init(&sink->ep.value);
	buf_set_str(&sink->ep.value, "");

	if (local) {
		sink->ep.flag |= HK_FLAG_LOCAL;
                hk_ep_set_widget(HK_EP(sink), "switch-slide");
	}
        else {
                hk_ep_set_widget(HK_EP(sink), "led-green");
        }

	hk_tab_init(&sink->handlers, sizeof(hk_sink_handler_t));

	return sink;
}


static void hk_sink_cleanup(hk_sink_t *sink)
{
        hk_ep_cleanup(&sink->ep);
        hk_tab_cleanup(&sink->handlers);
}


static int hk_sink_free(void *user_data, hk_sink_t *sink)
{
        hk_sink_cleanup(sink);
        free(sink);
        return 1;
}


void hk_sink_add_handler(hk_sink_t *sink, hk_ep_func_t func, void *user_data)
{
        hk_sink_handler_t *handler = hk_tab_push(&sink->handlers);
        handler->func = func;
        handler->user_data = user_data;
}


static void hk_sink_local_connect(hk_sink_t *sink, hk_source_t *source)
{
        if (sink->local_source == NULL) {
                hk_sink_t **psink = hk_tab_push(&source->local_sinks);
                *psink = sink;
                sink->local_source = HK_EP(source);
                log_debug(2, "hk_sink_local_connect %s: sink #%d connected to source #%d",
                          sink->ep.obj->name, sink->ep.id, source->ep.id);
        }
        else {
                log_str("WARNING: Cannot connect sink %s.%s to local source %s.%s: already connected to %s.%s",
                        sink->ep.obj->tile->name, sink->ep.obj->name,
                        source->ep.obj->tile->name, source->ep.obj->name,
                        sink->local_source->obj->tile->name, sink->local_source->obj->name);
        }
}


hk_sink_t *hk_sink_register(hk_endpoints_t *eps, hk_obj_t *obj, int local)
{
	hk_sink_t *sink;

	/* Ensure there is not sink with this name */
        sink = hk_sink_retrieve_by_name(eps, obj->name);
	if (sink != NULL) {
		log_str("ERROR: Cannot register sink '%s': sink #%d is already registered with this name\n", obj->name, sink->ep.id);
		return NULL;
	}

	/* Allocate new sink */
	sink = hk_sink_alloc(eps, obj, local);
	log_debug(2, "hk_sink_register '%s' #%d (%d elements)", obj->name, sink->ep.id, eps->sinks.nmemb);

        /* Establish local connection with source, if any */
        if (!local) {
                hk_source_t *source = hk_source_retrieve_by_name(eps, obj->name);
                if (source != NULL) {
                        hk_sink_local_connect(sink, source);
                }
        }

	return sink;
}


void hk_sink_set_widget(hk_sink_t *sink, char *widget_name)
{
	if (widget_name != NULL) {
		hk_ep_set_widget(HK_EP(sink), widget_name);
	}
}


#if 0
static void hk_sink_unregister(hk_endpoints_t *eps, char *name)
{
	hk_sink_t *sink = hk_sink_retrieve_by_name(eps, name);
	int i, j;

	if (sink == NULL) {
                return;
        }

        log_debug(2, "hk_sink_unregister '%s' #%d", name, sink->ep.id);

        /* Cancel connections to local sources */
        for (i = 0; i < eps->sources.nmemb; i++) {
                hk_source_t *source = HK_TAB_VALUE(eps->sources, hk_source_t *, i);
                for (j = 0; j < source->local_sinks.nmemb; j++) {
                        hk_sink_t **psink = HK_TAB_PTR(source->local_sinks, hk_sink_t *, j);
                        if (*psink == sink) {
                                *psink = NULL;
                        }
                }
        }

        /* Cleanup sink */
        hk_sink_cleanup(sink);
}
#endif


int hk_sink_is_public(hk_sink_t *sink)
{
        return hk_ep_is_public(HK_EP(sink));
}


char *hk_sink_update(hk_sink_t *sink, char *value)
{
        char *name = sink->ep.obj->name;
	int i;

        /* Prevent circular endoint update */
        if (sink->ep.locked) {
		log_str("WARNING: Attempting to update locked sink endpoint '%s' #%d", name, sink->ep.id);
                return name;
        }

	/* Update sink value */
	buf_set_str(&sink->ep.value, value);

	/* Invoke sink event callback */
        sink->ep.locked = 1;

	for (i = 0; i < sink->handlers.nmemb; i++) {
		hk_sink_handler_t *handler = HK_TAB_PTR(sink->handlers, hk_sink_handler_t, i);
		if (handler->func != NULL) {
			handler->func(handler->user_data, &sink->ep);
		}
	}

        sink->ep.locked = 0;

	return name;
}


int hk_sink_id(hk_sink_t *sink)
{
        return hk_ep_id(HK_EP(sink));
}


void hk_sink_update_by_name(hk_endpoints_t *eps, char *name, char *value)
{
	hk_sink_t *sink = hk_sink_retrieve_by_name(eps, name);

	if (sink != NULL) {
		log_debug(2, "hk_sink_update %s='%s'", name, value);
		hk_sink_update(sink, value);
	}
	else {
		log_str("WARNING: Attempting to update unkown sink '%s'", name);
	}
}


void hk_sink_foreach(hk_endpoints_t *eps, hk_ep_foreach_func_t func, void *user_data)
{
	int i;

	for (i = 0; i < eps->sinks.nmemb; i++) {
		hk_sink_t *sink = HK_TAB_VALUE(eps->sinks, hk_sink_t *, i);
                if ((sink != NULL) && (sink->ep.obj != NULL)) {
                        if (func(user_data, HK_EP(sink)) == 0) {
                                return;
                        }
                }
	}
}


void hk_sink_foreach_public(hk_endpoints_t *eps, hk_ep_func_t func, void *user_data)
{
	int i;

	for (i = 0; i < eps->sinks.nmemb; i++) {
		hk_sink_t *sink = HK_TAB_VALUE(eps->sinks, hk_sink_t *, i);
                if ((sink != NULL) && (sink->ep.obj != NULL)) {
                        if ((sink->ep.flag & HK_FLAG_LOCAL) == 0) {
                                func(user_data, &sink->ep);
                        }
                }
	}
}


/*
 * Sources
 */

int hk_source_to_advertise(hk_endpoints_t *eps)
{
	int count = 0;
	int i;

	for (i = 0; i < eps->sources.nmemb; i++) {
		hk_source_t *source = HK_TAB_VALUE(eps->sources, hk_source_t *, i);
                if (hk_source_is_public(source)) {
			count++;
		}
	}

	return count;
}


hk_source_t *hk_source_retrieve_by_id(hk_endpoints_t *eps, int id)
{
	if (id < 0) {
		return NULL;
	}

	if (id >= eps->sources.nmemb) {
		return NULL;
	}
	
	hk_source_t *source = HK_TAB_VALUE(eps->sources, hk_source_t *, id);

	if (source->ep.obj == NULL) {
		return NULL;
	}

	return source;
}


hk_source_t *hk_source_retrieve_by_name(hk_endpoints_t *eps, char *name)
{
	int i;

	for (i = 0; i < eps->sources.nmemb; i++) {
		hk_source_t *source = HK_TAB_VALUE(eps->sources, hk_source_t *, i);
		if ((source != NULL) && (source->ep.obj != NULL)) {
			if (strcmp(source->ep.obj->name, name) == 0) {
				return source;
			}
		}
	}

	return NULL;
}


static hk_source_t *hk_source_alloc(hk_endpoints_t *eps, hk_obj_t *obj, int local, int event)
{
	hk_source_t *source = NULL;
	int i;

        /* Search a free entry in the source table */
	for (i = 0; (i < eps->sources.nmemb) && (source == NULL); i++) {
		source = HK_TAB_VALUE(eps->sources, hk_source_t *, i);
		if (source->ep.obj != NULL) {
                        source = NULL;
		}
	}

        /* If no free entry found, create a new one */
        if (source == NULL) {
                hk_source_t **psource = hk_tab_push(&eps->sources);
                *psource = source = (hk_source_t *) malloc(sizeof(hk_source_t));
                memset(source, 0, sizeof(hk_source_t));
        }

	source->ep.type = HK_EP_SOURCE;
	source->ep.id = i;
	source->ep.obj = obj;
	buf_init(&source->ep.value);
	buf_set_str(&source->ep.value, "");

	if (local) {
		source->ep.flag |= HK_FLAG_LOCAL;
	}

	if (event) {
		source->ep.flag |= HK_FLAG_EVENT;
	}

        hk_ep_set_widget(HK_EP(source), "led-red");

	hk_tab_init(&source->local_sinks, sizeof(hk_sink_t *));

	return source;
}


static void hk_source_cleanup(hk_source_t *source)
{
        hk_ep_cleanup(&source->ep);
        hk_tab_cleanup(&source->local_sinks);
}


static int hk_source_free(void *user_data, hk_source_t *source)
{
        hk_source_cleanup(source);
        free(source);
        return 1;
}


hk_source_t *hk_source_register(hk_endpoints_t *eps, hk_obj_t *obj, int local, int event)
{
	hk_source_t *source;

	/* Ensure there is not source with this name */
        source = hk_source_retrieve_by_name(eps, obj->name);
	if (source != NULL) {
		log_str("ERROR: Cannot register source '%s': source #%d is already registered with this name\n", obj->name, source->ep.id);
		return NULL;
	}

	/* Allocate new source */
	source = hk_source_alloc(eps, obj, local, event);
	log_debug(2, "hk_source_register '%s' #%d (%d elements)", obj->name, source->ep.id, eps->sources.nmemb);

        /* Establish local connection with sink, if any */
        hk_sink_t *sink = hk_sink_retrieve_by_name(eps, obj->name);
	if (sink != NULL) {
                hk_sink_local_connect(sink, source);
	}

	return source;
}


void hk_source_set_widget(hk_source_t *source, char *widget_name)
{
	if (widget_name != NULL) {
		hk_ep_set_widget(HK_EP(source), widget_name);
	}
}


#if 0
void hk_source_unregister(hk_endpoints_t *eps, char *name)
{
	hk_source_t *source = hk_source_retrieve_by_name(eps, name);

	if (source == NULL) {
                return;
	}

        log_debug(2, "hk_source_unregister '%s' #%d", name, source->ep.id);

        /* Cleanup source */
        hk_source_cleanup(source);
}
#endif


int hk_source_is_public(hk_source_t *source)
{
        return hk_ep_is_public(HK_EP(source));
}


int hk_source_is_event(hk_source_t *source)
{
        if (source == NULL) {
                return 0;
        }
        if (source->ep.obj == NULL) {
                return 0;
        }
	return (source->ep.flag & HK_FLAG_EVENT) ? 1:0;
}


void hk_source_foreach(hk_endpoints_t *eps, hk_ep_foreach_func_t func, void *user_data)
{
	int i;

	for (i = 0; i < eps->sources.nmemb; i++) {
		hk_source_t *source = HK_TAB_VALUE(eps->sources, hk_source_t *, i);
                if (func(user_data, HK_EP(source)) == 0) {
                        return;
                }
	}
}


void hk_source_foreach_public(hk_endpoints_t *eps, hk_ep_func_t func, void *user_data)
{
	int i;

	for (i = 0; i < eps->sources.nmemb; i++) {
		hk_source_t *source = HK_TAB_VALUE(eps->sources, hk_source_t *, i);
		if ((source->ep.obj != NULL) && ((source->ep.flag & HK_FLAG_LOCAL) == 0)) {
			////int event = (source->ep.flag & HK_FLAG_EVENT) ? 1:0;
			////func(user_data, source->ep.obj->name, (char *) source->ep.value.base, event);
			func(user_data, &source->ep);
		}
	}
}


char *hk_source_update(hk_source_t *source, char *value)
{
        char *name = source->ep.obj->name;
        int i;

        /* Prevent circular endoint update */
        if (source->ep.locked) {
		log_str("WARNING: Attempting to update locked endpoint '%s' #%d", name, source->ep.id);
                return name;
        }

        /* Update value */
        buf_set_str(&source->ep.value, value);

        /* Invoke update handlers of locally connected sinks */
        source->ep.locked = 1;

        for (i = 0; i < source->local_sinks.nmemb; i++) {
                hk_sink_t *sink = HK_TAB_VALUE(source->local_sinks, hk_sink_t *, i);
                if (sink != NULL) {
                        hk_sink_update(sink, value);
                }
        }

        source->ep.locked = 0;

	return name;
}


int hk_source_id(hk_source_t *source)
{
        return hk_ep_id(HK_EP(source));
}


/*
 * Endpoint collection
 */

int hk_endpoints_init(hk_endpoints_t *eps)
{
	hk_tab_init(&eps->sinks, sizeof(hk_sink_t *));
	hk_tab_init(&eps->sources, sizeof(hk_source_t *));
	return 0;
}


void hk_endpoints_shutdown(hk_endpoints_t *eps)
{
        hk_sink_foreach(eps, (hk_ep_foreach_func_t) hk_sink_free, NULL);
	hk_tab_cleanup(&eps->sinks);

        hk_source_foreach(eps, (hk_ep_foreach_func_t) hk_source_free, NULL);
	hk_tab_cleanup(&eps->sources);
}
