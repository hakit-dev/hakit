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

#ifndef __HAKIT_ENDPOINT_H__
#define __HAKIT_ENDPOINT_H__

#include "buf.h"
#include "tab.h"
#include "mod.h"


/*
 * Endpoint flags
 */

#define HK_FLAG_EVENT   0x0001
#define HK_FLAG_LOCAL   0x0002


/*
 * Endpoint collection
 */

typedef struct {
	hk_tab_t sinks;       // Table of (hk_sink_t *)
	hk_tab_t sources;     // Table of (hk_source_t *)
} hk_endpoints_t;

extern int hk_endpoints_init(hk_endpoints_t *eps);
extern void hk_endpoints_shutdown(hk_endpoints_t *eps);


/*
 * Generic endpoint dataset
 */

#define HK_EP(ep) ((hk_ep_t *)(ep))

typedef enum {
	HK_EP_SINK=0,
	HK_EP_SOURCE,
	HK_EP_NTYPES
} hk_ep_type_t;

typedef struct {
	hk_ep_type_t type;
	int id;                  /* Endpoint id */
	hk_obj_t *obj;
	buf_t value;
	unsigned int flag;
	char *widget;
	char *chart;
        int locked;
} hk_ep_t;

typedef int (*hk_ep_foreach_func_t)(void *user_data, hk_ep_t *ep);

#define hk_ep_get_tile_name(ep) ((ep)->obj->tile->name)
#define hk_ep_get_name(ep) ((ep)->obj->name)
#define hk_ep_get_value(ep) ((char *) ((ep)->value.base))
#define hk_ep_flag_retain(ep) ((((ep)->flag) & HK_FLAG_EVENT) ? 0:1)
extern void hk_ep_append_name(hk_ep_t *ep, buf_t *out_buf);
extern void hk_ep_append_value(hk_ep_t *ep, buf_t *out_buf);
extern void hk_ep_dump(hk_ep_t *ep, buf_t *out_buf);
extern void hk_ep_set_widget(hk_ep_t *ep, char *widget_name);
extern void hk_ep_set_chart(hk_ep_t *ep, char *chart_name);


/*
 * Sink endpoint
 */

typedef void (*hk_ep_func_t)(void *user_data, hk_ep_t *ep);

typedef struct {
	hk_ep_func_t func;
	void *user_data;
} hk_sink_handler_t;

typedef struct {
	hk_ep_t ep;
	hk_tab_t handlers;   // Table of (hk_sink_handler_t);
        hk_ep_t *local_source;
} hk_sink_t;

extern hk_sink_t *hk_sink_register(hk_endpoints_t *eps, hk_obj_t *obj, int local);
extern hk_sink_t *hk_sink_retrieve_by_name(hk_endpoints_t *eps, char *name);
extern hk_sink_t *hk_sink_retrieve_by_id(hk_endpoints_t *eps, int id);
extern void hk_sink_update_by_name(hk_endpoints_t *eps, char *name, char *value);
extern void hk_sink_foreach(hk_endpoints_t *eps, hk_ep_foreach_func_t func, void *user_data);
extern void hk_sink_foreach_public(hk_endpoints_t *eps, hk_ep_func_t func, void *user_data);

extern int hk_sink_id(hk_sink_t *sink);
extern void hk_sink_add_handler(hk_sink_t *sink, hk_ep_func_t func, void *user_data);
extern int hk_sink_is_public(hk_sink_t *sink);
extern char *hk_sink_update(hk_sink_t *sink, char *value);


/*
 * Source endpoint
 */

typedef struct {
	hk_ep_t ep;
	hk_tab_t local_sinks;   // Table of (hk_sink_t *);
} hk_source_t;

extern hk_source_t *hk_source_register(hk_endpoints_t *eps, hk_obj_t *obj, int local, int event);
extern hk_source_t *hk_source_retrieve_by_name(hk_endpoints_t *eps, char *name);
extern hk_source_t *hk_source_retrieve_by_id(hk_endpoints_t *eps, int id);
extern int hk_source_to_advertise(hk_endpoints_t *eps);
extern void hk_source_foreach(hk_endpoints_t *eps, hk_ep_foreach_func_t func, void *user_data);
extern void hk_source_foreach_public(hk_endpoints_t *eps, hk_ep_func_t func, void *user_data);

extern int hk_source_id(hk_source_t *source);
extern int hk_source_is_public(hk_source_t *source);
extern int hk_source_is_event(hk_source_t *source);
extern char *hk_source_update(hk_source_t *source, char *value);

#endif /* __HAKIT_ENDPOINT_H__ */
