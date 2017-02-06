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

#include "hkcp.h"
typedef hkcp_sink_func_t comm_sink_func_t;

extern int comm_init(int use_ssl, int use_hkcp, char *hkcp_hosts);
extern void comm_monitor(comm_sink_func_t func, void *user_data);

extern int comm_sink_register(char *name, comm_sink_func_t func, void *user_data);
extern void comm_sink_set_local(int id);
extern void comm_sink_set_widget(int id, char *widget_name);
extern void comm_sink_update_str(int id, char *value);
extern void comm_sink_update_int(int id, int value);

extern int comm_source_register(char *name, int event);
extern void comm_source_set_local(int id);
extern void comm_source_set_widget(int id, char *widget_name);
extern void comm_source_update_str(int id, char *value);
extern void comm_source_update_int(int id, int value);


//
// HTTP/HTTPS client
//
typedef void comm_recv_func_t(void *user_data, char *buf, int len);
extern int comm_wget(char *uri, comm_recv_func_t *func, void *user_data);

#endif /* __HAKIT_COMM_H__ */
