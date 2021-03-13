/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2021 Sylvain Giroudon
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
#include "tcpio.h"
#include "tab.h"
#include "endpoint.h"

typedef struct hkcp_s hkcp_t;


/*
 * Nodes
 */

#define HKCP_NODE_CONNECT_RETRIES 4

typedef enum {
	HKCP_NODE_IDLE=0,
	HKCP_NODE_CONNECT,
	HKCP_NODE_SINKS,
	HKCP_NODE_READY,
	HKCP_NODE_NSTATES
} hkcp_node_state_t;

typedef struct {
	int id;
	char *name;
	tcp_sock_t tcp_sock;
	hkcp_node_state_t state;
	int connect_attempts;
	sys_tag_t timeout_tag;
	hk_tab_t sources;       // Table of (hk_source_t *)
	hkcp_t *hkcp;
	buf_t rbuf;
} hkcp_node_t;


/*
 * Communication engine
 */

struct hkcp_s {
	int port;
        char *certs;
	tcp_srv_t tcp_srv;
	hk_tab_t nodes;       // Table of (hkcp_node_t *)
};

extern int hkcp_init(hkcp_t *hkcp, int port, char *certs);
extern void hkcp_shutdown(hkcp_t *hkcp);
extern void hkcp_node_add(hkcp_t *hkcp, char *remote_ip);
extern void hkcp_node_dump(hkcp_t *hkcp, hk_source_t *source, buf_t *out_buf);

extern void hkcp_source_update(hkcp_t *hkcp, hk_source_t *source, char *value);

#endif /* __HAKIT_HKCP_H__ */
