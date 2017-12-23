/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_COMM_H__
#define __HAKIT_COMM_H__

#include "mod.h"
#include "endpoint.h"

extern int comm_init(int use_ssl, char *certs,
                     int use_hkcp,
                     int use_mqtt, char *mqtt_broker);

extern int comm_tile_register(char *path);

extern int comm_sink_register(hk_obj_t *obj, int local, hk_ep_func_t func, void *user_data);
extern void comm_sink_set_widget(int id, char *widget_name);
extern void comm_sink_update_str(int id, char *value);
extern void comm_sink_update_int(int id, int value);

extern int comm_source_register(hk_obj_t *obj, int local, int event);
extern void comm_source_set_widget(int id, char *widget_name);
extern void comm_source_update_str(int id, char *value);
extern void comm_source_update_int(int id, int value);


//
// HTTP/HTTPS client
//
typedef void comm_recv_func_t(void *user_data, char *buf, int len);
extern int comm_wget(char *uri, comm_recv_func_t *func, void *user_data);

#endif /* __HAKIT_COMM_H__ */
