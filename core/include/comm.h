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

extern int comm_init(int use_ssl, char *certs, int use_hkcp, int advertise);
extern int comm_enable_mqtt(char *certs, char *mqtt_broker);
extern void comm_set_trace_depth(int depth);

extern int comm_tile_register(char *path);

extern hk_sink_t *comm_sink_register(hk_obj_t *obj, int local, hk_ep_func_t func, void *user_data);
extern void comm_sink_update_str(hk_sink_t *sink, char *value);

extern hk_source_t *comm_source_register(hk_obj_t *obj, int local, int event);
extern void comm_source_update_str(hk_source_t *source, char *value);

#endif /* __HAKIT_COMM_H__ */
