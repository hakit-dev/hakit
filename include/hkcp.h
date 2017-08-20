/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * HAKit Connectivity Protocol
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_HKCP_H__
#define __HAKIT_HKCP_H__

#include "sys.h"
#include "buf.h"
#include "netif.h"
#include "tcpio.h"
#include "udpio.h"
#include "tab.h"
#include "command.h"

typedef struct hkcp_s hkcp_t;


/*
 * Source/Sink flags
 */

#define HKCP_FLAG_EVENT   0x0001
#define HKCP_FLAG_LOCAL   0x0002


/*
 * Nodes
 */

typedef struct {
	int id;
	char *name;
	tcp_sock_t tcp_sock;
	int connect_attempts;
	sys_tag_t timeout_tag;
	hkcp_t *hkcp;
	command_t *cmd;
	int watch;
} hkcp_node_t;


/*
 * Generic endpoint dataset
 */

typedef enum {
	HKCP_EP_SINK=0,
	HKCP_EP_SOURCE,
	HKCP_EP_NTYPES
} hkcp_ep_type_t;

typedef struct {
	hkcp_ep_type_t type;
	int id;                  /* Endpoint id */
	char *name;              /* Endpoint name */
	buf_t value;
	unsigned int flag;
	char *widget;
} hkcp_ep_t;


/*
 * Sink endpoint
 */

typedef void (*hkcp_sink_func_t)(void *user_data, char *name, char *value);

typedef struct {
	hkcp_sink_func_t func;
	void *user_data;
} hkcp_sink_handler_t;

typedef struct {
	hkcp_ep_t ep;
	hk_tab_t handlers;   // Table of (hkcp_sink_handler_t);
} hkcp_sink_t;


/*
 * Source endpoint
 */

typedef struct {
	hkcp_ep_t ep;
	hk_tab_t nodes;       // Table of (hkcp_node_t *)
} hkcp_source_t;


/*
 * Communication engine
 */

struct hkcp_s {
	netif_env_t ifs;
	udp_srv_t udp_srv;
	tcp_srv_t tcp_srv;
	hk_tab_t hosts;       // Table of (char *)
	hk_tab_t nodes;       // Table of (hkcp_node_t *)
	hk_tab_t sinks;       // Table of (hkcp_sink_t)
	hk_tab_t sources;     // Table of (hkcp_source_t)
	sys_tag_t advertise_tag;
};

extern int hkcp_init(hkcp_t *hkcp, int port, char *hosts);
extern void hkcp_command(hkcp_t *hkcp, int argc, char **argv, buf_t *out_buf);

extern int hkcp_sink_register(hkcp_t *hkcp, char *name);
extern void hkcp_sink_add_handler(hkcp_t *hkcp, int id, hkcp_sink_func_t func, void *user_data);
extern void hkcp_sink_set_local(hkcp_t *hkcp, int id);
extern void hkcp_sink_set_widget(hkcp_t *hkcp, int id, char *widget_name);
extern char *hkcp_sink_update(hkcp_t *hkcp, int id, char *value);
extern void hkcp_sink_update_by_name(hkcp_t *hkcp, char *name, char *value);

extern int hkcp_source_register(hkcp_t *hkcp, char *name, int event);
extern void hkcp_source_set_local(hkcp_t *hkcp, int id);
extern void hkcp_source_set_widget(hkcp_t *hkcp, int id, char *widget_name);
extern int hkcp_source_is_event(hkcp_t *hkcp, int id);
extern char *hkcp_source_update(hkcp_t *hkcp, int id, char *value);

#endif /* __HAKIT_HKCP_H__ */
