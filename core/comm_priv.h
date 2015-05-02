/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_COMM_PRIV_H__
#define __HAKIT_COMM_PRIV_H__

#include "sys.h"
#include "buf.h"
#include "tcpio.h"
#include "udpio.h"
#include "tab.h"
#include "command.h"
#include "comm.h"

typedef struct comm_s comm_t;


/*
 * Nodes
 */

typedef struct {
	int id;
	char *name;
	tcp_sock_t tcp_sock;
	int connect_attempts;
	sys_tag_t timeout_tag;
	comm_t *comm;
	command_t *cmd;
} comm_node_t;


/*
 * Sinks
 */

typedef struct {
	char *name;              /* Sink name */
	buf_t value;
	comm_sink_func_t func;   /* Handler to call when sink is updated */
	void *user_data;
} comm_sink_t;


/*
 * Sources
 */

typedef struct {
	int id;
	char *name;
	buf_t value;
	int event;
	hk_tab_t nodes;       // Table of (comm_node_t *)
} comm_source_t;


/*
 * Communication engine
 */

struct comm_s {
	udp_srv_t udp_srv;
	tcp_srv_t tcp_srv;
	hk_tab_t nodes;       // Table of (comm_node_t *)
	hk_tab_t sinks;       // Table of (comm_sink_t)
	hk_tab_t sources;     // Table of (comm_source_t)
	sys_tag_t advertise_tag;
	io_channel_t chan_stdin;
	comm_sink_func_t monitor_func;
	void *monitor_user_data;
};

#endif /* __HAKIT_COMM_PRIV_H__ */