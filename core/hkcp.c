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

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>

#include "log.h"
#include "buf.h"
#include "netif.h"
#include "iputils.h"
#include "tcpio.h"
#include "udpio.h"
#include "command.h"
#include "hakit_version.h"
#include "advertise.h"
#include "hkcp.h"


/* TCP Command context */
typedef struct {
	hkcp_t *hkcp;
	tcp_sock_t *tcp_sock;
	command_t *cmd;
	int watch;
} hkcp_command_ctx_t;


/* Local functions forward declarations */
static int hkcp_node_connect(hkcp_node_t *node);
static void hkcp_node_remove(hkcp_node_t *node);

static hkcp_source_t *hkcp_source_retrieve(hkcp_t *hkcp, char *name);
static void hkcp_source_send_initial_value(hkcp_source_t *source, hkcp_node_t *node);
static void hkcp_source_attach_node(hkcp_source_t *source, hkcp_node_t *node);


/*
 * Nodes
 */

static hkcp_node_t *hkcp_node_retrieve(hkcp_t *hkcp, char *name)
{
	int i;

	for (i = 0; i < hkcp->nodes.nmemb; i++) {
		hkcp_node_t *node = HK_TAB_VALUE(hkcp->nodes, hkcp_node_t *, i);
		if (node != NULL) {
			if (strcmp(node->name, name) == 0) {
				return node;
			}
		}
	}

	return NULL;
}


static hkcp_node_t *hkcp_node_alloc(hkcp_t *hkcp)
{
	hkcp_node_t *node;
	hkcp_node_t **pnode = NULL;
	int i;

	/* Alloc node descriptor */
	node = (hkcp_node_t *) malloc(sizeof(hkcp_node_t));
	memset(node, 0, sizeof(hkcp_node_t));

	/* Find entry in node table */
	for (i = 0; i < hkcp->nodes.nmemb; i++) {
		pnode = HK_TAB_PTR(hkcp->nodes, hkcp_node_t *, i);
		if (*pnode == NULL) {
			log_debug(2, "hkcp_node_alloc -> #%d (reused)", i);
			break;
		}
	}

	if (i >= hkcp->nodes.nmemb) {
		log_debug(2, "hkcp_node_alloc -> #%d (new)", i);
		pnode = hk_tab_push(&hkcp->nodes);
	}

	if (pnode != NULL) {
		*pnode = node;
	}
	else {
		log_str("PANIC: node entry allocation failed");
	}

	/* Init node entry */
	node->id = i;
	tcp_sock_clear(&node->tcp_sock);
	node->hkcp = hkcp;
	buf_init(&node->rbuf);

	return node;
}


static void hkcp_node_set_state(hkcp_node_t *node, hkcp_node_state_t state)
{
	log_debug(2, "hkcp_node_set_state node=#%d='%s' %d -> %d", node->id, node->name, node->state, state);
	node->state = state;
	node->rbuf.len = 0;
}


static int hkcp_node_attached_to_sources(hkcp_node_t *node)
{
	int i, j;

	for (i = 0; i < node->hkcp->sources.nmemb; i++) {
		hkcp_source_t *source = HK_TAB_PTR(node->hkcp->sources, hkcp_source_t, i);
		if (source->ep.name != NULL) {
			for (j = 0; j < source->nodes.nmemb; j++) {
				hkcp_node_t **pnode = HK_TAB_PTR(source->nodes, hkcp_node_t *, j);
				if (*pnode == node) {
					return 1;
				}
			}
		}
	}

	return 0;
}


static void hkcp_node_recv_version(hkcp_node_t *node, char *str)
{
	log_debug(2, "hkcp_node_recv_version node=#%d='%s' str='%s'", node->id, node->name, str);

	if (*str == '.') {
		/* Send command to get list of sinks */
		hkcp_node_set_state(node, HKCP_NODE_SINKS);
		tcp_sock_write(&node->tcp_sock, "sinks\n", 6);
	}
	else if (node->version == NULL) {
		node->version = strdup(str);
		log_debug(2, " => '%s'", node->version);
	}
}


static void hkcp_node_recv_sinks(hkcp_node_t *node, char *str)
{
	log_debug(2, "hkcp_node_recv_sinks node=#%d='%s' str='%s'", node->id, node->name, str);

	if (*str == '.') {
		/* If no sources attached to this node, remove it */
		if (hkcp_node_attached_to_sources(node)) {
			hkcp_node_set_state(node, HKCP_NODE_READY);
		}
		else {
			hkcp_node_remove(node);
		}
	}
	else {
		/* Mark sink name delimiter */
		char *sp = strchr(str, ' ');
		if (sp != NULL) {
			*(sp++) = '\0';
		}
		
		/* Check for local sources matching this remote sink */
		hkcp_source_t *source = hkcp_source_retrieve(node->hkcp, str);

		/* If matching source is found, check for requesting node connection */
		if (source != NULL) {
			if ((source->ep.flag & HKCP_FLAG_LOCAL) == 0) {
				log_debug(2, "  remote sink='%s', matching source='%s'", str, source->ep.name);

				/* Attach source to node (if not already done) */
				hkcp_source_attach_node(source, node);

				/* Send initial value */
				hkcp_source_send_initial_value(source, node);
			}
			else {
				log_debug(2, "  remote sink='%s', matching source='%s' (local)", str, source->ep.name);
			}
		}
		else {
			log_debug(2, "  remote sink='%s', no source found", str);
		}
	}
}


static void hkcp_node_recv_line(hkcp_node_t *node, char *str)
{
	log_debug(3, "hkcp_node_recv_line node=#%d='%s' state=%d str='%s'", node->id, node->name, node->state, str);

	switch (node->state) {
	case HKCP_NODE_VERSION:
		hkcp_node_recv_version(node, str);
		break;
	case HKCP_NODE_SINKS:
		hkcp_node_recv_sinks(node, str);
		break;
	default:
		break;
	}
}


static void hkcp_node_recv(hkcp_node_t *node, char *rbuf, int rsize)
{
	int i;

	log_debug(2, "hkcp_node_recv node=#%d='%s' rsize=%d", node->id, node->name, rsize);

	if (rsize <= 0) {
		return;
	}

	i = 0;
	while (i < rsize) {
		int i0 = i;

		/* Search end-of-line delimiter */
		while ((i < rsize) && (rbuf[i] != '\n')) {
			i++;
		}

		// If newline character reached, process this line
		if (i < rsize) {
			char *str = &rbuf[i0];
			int len = i - i0;

			rbuf[i++] = '\0';

			if (node->rbuf.len > 0) {
				buf_append(&node->rbuf, (unsigned char *) str, len);
				str = (char *) node->rbuf.base;
				len = node->rbuf.len;
				node->rbuf.len = 0;
			}

			if (len > 0) {
				hkcp_node_recv_line(node, str);
			}
		}
		else {
			buf_append(&node->rbuf, (unsigned char *) &rbuf[i0], i-i0);
		}	
	}
}


static void hkcp_node_event(tcp_sock_t *tcp_sock, tcp_io_t io, char *rbuf, int rsize)
{
	hkcp_node_t *node = tcp_sock_get_data(tcp_sock);
	log_debug(2, "hkcp_node_event [%d] node=#%d='%s'", tcp_sock->chan.fd, node->id, node->name);
	log_debug_data((unsigned char *) rbuf, rsize);

	switch (io) {
	case TCP_IO_CONNECT:
		log_debug(2, "  CONNECT %s", rbuf);
		break;
	case TCP_IO_DATA:
		log_debug(2, "  DATA %d", rsize);
		hkcp_node_recv(node, rbuf, rsize);
		break;
	case TCP_IO_HUP:
		log_debug(2, "  HUP");

		/* Try to reconnect immediately */
		hkcp_node_set_state(node, HKCP_NODE_IDLE);
		node->connect_attempts = 0;
		hkcp_node_connect(node);
		break;
	default:
		log_debug(2, "  PANIC: unknown event caught");
		break;
	}
}


static void hkcp_node_detach_from_sources(hkcp_t *hkcp, hkcp_node_t *node)
{
	int i, j;

	log_debug(2, "hkcp_node_detach_from_sources node=#%d='%s'", node->id, node->name);

	for (i = 0; i < hkcp->sources.nmemb; i++) {
		hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
		if (source->ep.name != NULL) {
			for (j = 0; j < source->nodes.nmemb; j++) {
				hkcp_node_t **pnode = HK_TAB_PTR(source->nodes, hkcp_node_t *, j);
				if (*pnode == node) {
					log_debug(2, "  source='%s'", source->ep.name);
					*pnode = NULL;
				}
			}
		}
	}
}


static void hkcp_node_remove(hkcp_node_t *node)
{
	log_debug(2, "hkcp_node_remove node=#%d='%s'", node->id, node->name);

	/* Free node entry for future use */
	hkcp_node_t **pnode = HK_TAB_PTR(node->hkcp->nodes, hkcp_node_t *, node->id);
	*pnode = NULL;

	/* Kill running timeout */
	if (node->timeout_tag) {
		sys_remove(node->timeout_tag);
		node->timeout_tag = 0;
	}

	/* Detach from all sources */
	hkcp_node_detach_from_sources(node->hkcp, node);

	/* Shut down connection */
	tcp_sock_set_data(&node->tcp_sock, NULL);
	tcp_sock_shutdown(&node->tcp_sock);

	/* Clear data buffering */
	if (node->version != NULL) {
		free(node->version);
	}
	buf_cleanup(&node->rbuf);

	/* Free node name */
	free(node->name);

	/* Free node descriptor */
	memset(node, 0, sizeof(hkcp_node_t));
	free(node);
}


static int hkcp_node_connect(hkcp_node_t *node)
{
	node->connect_attempts++;
	hkcp_node_set_state(node, HKCP_NODE_CONNECT);

	log_str("Connecting to node #%d='%s' (%d/%d)", node->id, node->name, node->connect_attempts, HKCP_NODE_CONNECT_RETRIES);
	if (tcp_sock_connect(&node->tcp_sock, node->name, node->hkcp->port, hkcp_node_event, node) > 0) {
		node->timeout_tag = 0;
		/* Get list of sinks from this node */
		hkcp_node_set_state(node, HKCP_NODE_VERSION);
		tcp_sock_write(&node->tcp_sock, "version\n", 8);
		return 0;
	}

	if (node->connect_attempts > HKCP_NODE_CONNECT_RETRIES) {
		log_str("Too many connections attempted on node #%d='%s': giving up", node->id, node->name);
		node->timeout_tag = 0;
		hkcp_node_remove(node);
		return 0;
	}

	if (node->timeout_tag == 0) {
		node->timeout_tag = sys_timeout(5000, (sys_func_t) hkcp_node_connect, node);
	}

	return 1;
}


static int hkcp_node_connect_first(hkcp_node_t *node)
{
	node->timeout_tag = 0;
	node->connect_attempts = 0;
	hkcp_node_connect(node);
	return 0;
}


static hkcp_node_t *hkcp_node_add(hkcp_t *hkcp, char *name)
{
	log_debug(2, "hkcp_node_add node='%s'", name);

	hkcp_node_t *node = hkcp_node_retrieve(hkcp, name);

	/* Allocate entry */
	if (node == NULL) {
		node = hkcp_node_alloc(hkcp);
		node->name = strdup(name);
	}

	if (!tcp_sock_is_connected(&node->tcp_sock)) {
		if (node->timeout_tag == 0) {
			node->timeout_tag = sys_timeout(10, (sys_func_t) hkcp_node_connect_first, node);
		}
	}

	return node;
}


/*
 * Generic endpoint operations
 */

#define HKCP_EP(ep) ((hkcp_ep_t *)(ep))

static inline const char *hkcp_ep_type_str(hkcp_ep_t *ep)
{
	static const char *tab[HKCP_EP_NTYPES] = {
		"sink", "source"
	};

	if (ep->type >= HKCP_EP_NTYPES) {
		return "?";
	}

	return tab[ep->type];
}


static void hkcp_ep_set_widget(hkcp_ep_t *ep, char *widget_name)
{
	if (ep->name != NULL) {
		log_debug(2, "hkcp_ep_set_widget_ '%s'", widget_name);
		if (ep->widget != NULL) {
			free(ep->widget);
			ep->widget = NULL;
		}
		if (widget_name != NULL) {
			ep->widget = strdup(widget_name);
		}
	}
	else {
		log_str("PANIC: Attempting to set widget name on unknown %s #%d\n", hkcp_ep_type_str(ep), ep->id);
	}
}


static void hkcp_ep_append_name(hkcp_ep_t *ep, buf_t *out_buf)
{
	buf_append_str(out_buf, ep->name);
}


static void hkcp_ep_dump(hkcp_ep_t *ep, buf_t *out_buf)
{
	buf_append_str(out_buf, (char *) hkcp_ep_type_str(ep));
	buf_append_byte(out_buf, ' ');
	buf_append_str(out_buf, ep->widget);
	buf_append_byte(out_buf, ' ');
	buf_append_str(out_buf, ep->name);
	buf_append_byte(out_buf, ' ');
	buf_append(out_buf, ep->value.base, ep->value.len);
	buf_append_byte(out_buf, '\n');
}


/*
 * Sinks
 */

static hkcp_sink_t *hkcp_sink_retrieve(hkcp_t *hkcp, char *name)
{
	int i;

	for (i = 0; i < hkcp->sinks.nmemb; i++) {
		hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, i);
		if (sink->ep.name != NULL) {
			if (strcmp(sink->ep.name, name) == 0) {
				return sink;
			}
		}
	}

	return NULL;
}


static hkcp_sink_t *hkcp_sink_alloc(hkcp_t *hkcp)
{
	hkcp_sink_t *sink;
	int i;

	for (i = 0; i < hkcp->sinks.nmemb; i++) {
		sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, i);
		if (sink->ep.name == NULL) {
			return sink;
		}
	}

	sink = hk_tab_push(&hkcp->sinks);

	sink->ep.type = HKCP_EP_SINK;
	sink->ep.id = i;
	buf_init(&sink->ep.value);

	return sink;
}


static void hkcp_sink_set_widget_name(hkcp_sink_t *sink, char *widget_name)
{
	if (widget_name == NULL) {
		if (sink->ep.flag & HKCP_FLAG_LOCAL) {
			widget_name = "switch-slide";
		}
		else {
			widget_name = "led-green";
		}
	}

	hkcp_ep_set_widget(HKCP_EP(sink), widget_name);
}


void hkcp_sink_add_handler(hkcp_t *hkcp, int id, hkcp_sink_func_t func, void *user_data)
{
	hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, id);

	if ((sink != NULL) && (sink->ep.name != NULL)) {
		hkcp_sink_handler_t *handler = hk_tab_push(&sink->handlers);
		handler->func = func;
		handler->user_data = user_data;
	}
	else {
		log_str("PANIC: Attempting to and event handler on unknown sink #%d\n", id);
	}
}


int hkcp_sink_register(hkcp_t *hkcp, char *name, int local)
{
	hkcp_sink_t *sink;

	/* Ensure there is no name conflict against sink and source namespaces */
	if (hkcp_source_retrieve(hkcp, name) != NULL) {
		log_str("ERROR: Cannot register sink '%s': a source is already registered with this name\n", name);
		return -1;
	}

	if (hkcp_sink_retrieve(hkcp, name)) {
		log_str("ERROR: Cannot register source '%s': a sink is already registered with this name\n", name);
		return -1;
	}

	/* Allocate new sink */
	sink = hkcp_sink_alloc(hkcp);
	sink->ep.name = strdup(name);
	log_debug(2, "hkcp_sink_register sink='%s' (%d elements)", name, hkcp->sinks.nmemb);

	buf_set_str(&sink->ep.value, "");
	hkcp_sink_set_widget_name(sink, NULL);

	hk_tab_init(&sink->handlers, sizeof(hkcp_sink_handler_t));

	if (local) {
		sink->ep.flag |= HKCP_FLAG_LOCAL;
	}

	/* Trigger advertising */
	if (!local) {
		hk_advertise_sink(&hkcp->adv);
	}

	return sink->ep.id;
}


void hkcp_sink_set_widget(hkcp_t *hkcp, int id, char *widget_name)
{
	hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, id);
	if (sink != NULL) {
		hkcp_sink_set_widget_name(sink, widget_name);
	}
	else {
		log_str("PANIC: Attempting to set widget on unknown sink #%d\n", id);
	}
}


#if 0
static void hkcp_sink_unregister_(hkcp_t *hkcp, char *name)
{
	hkcp_sink_t *sink = hkcp_sink_retrieve(hkcp, name);

	if (sink != NULL) {
		free(sink->ep.name);
		buf_cleanup(&sink->ep.value);
		sink->ep.flag = 0;
		hk_tab_cleanup(&sink->handlers);
	}
}
#endif


static char *hkcp_sink_update_(hkcp_sink_t *sink, char *value)
{
	int i;

	/* Update sink value */
	buf_set_str(&sink->ep.value, value);

	/* Invoke sink event callback */
	for (i = 0; i < sink->handlers.nmemb; i++) {
		hkcp_sink_handler_t *handler = HK_TAB_PTR(sink->handlers, hkcp_sink_handler_t, i);
		if (handler->func != NULL) {
			handler->func(handler->user_data, sink->ep.name, (char *) sink->ep.value.base);
		}
	}

	return sink->ep.name;
}


char *hkcp_sink_update(hkcp_t *hkcp, int id, char *value)
{
	hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, id);

	if ((sink == NULL) || (sink->ep.name == NULL)) {
		log_str("PANIC: Attempting to update unknown sink #%d\n", id);
		return NULL;
	}

	return hkcp_sink_update_(sink, value);
}


void hkcp_sink_update_by_name(hkcp_t *hkcp, char *name, char *value)
{
	hkcp_sink_t *sink = hkcp_sink_retrieve(hkcp, name);

	if (sink != NULL) {
		log_debug(2, "hkcp_sink_update %s='%s'", name, value);
		hkcp_sink_update_(sink, value);
	}
	else {
		log_str("WARNING: Attempting to update unkown sink '%s'", name);
	}
}


/*
 * Sources
 */

static int hkcp_source_to_advertise(hkcp_t *hkcp)
{
	int count = 0;
	int i;

	for (i = 0; i < hkcp->sources.nmemb; i++) {
		hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
		if ((source->ep.name != NULL) && ((source->ep.flag & HKCP_FLAG_LOCAL) == 0)) {
			count++;
		}
	}

	return count;
}


static hkcp_source_t *hkcp_source_retrieve(hkcp_t *hkcp, char *name)
{
	int i;

	for (i = 0; i < hkcp->sources.nmemb; i++) {
		hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
		if (source->ep.name != NULL) {
			if (strcmp(source->ep.name, name) == 0) {
				return source;
			}
		}
	}

	return NULL;
}


static hkcp_source_t *hkcp_source_alloc(hkcp_t *hkcp)
{
	hkcp_source_t *source = NULL;
	int i;

	for (i = 0; i < hkcp->sources.nmemb; i++) {
		source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
		if (source->ep.name == NULL) {
			source->ep.id = i;
			return source;
		}
	}

	i = hkcp->sources.nmemb;
	source = hk_tab_push(&hkcp->sources);

	source->ep.type = HKCP_EP_SOURCE;
	source->ep.id = i;
	buf_init(&source->ep.value);

	hk_tab_init(&source->nodes, sizeof(hkcp_node_t *));

	return source;
}


static void hkcp_source_set_widget_name(hkcp_source_t *source, char *widget_name)
{
	if (widget_name == NULL) {
		widget_name = "led-red";
	}

	hkcp_ep_set_widget(HKCP_EP(source), widget_name);
}


int hkcp_source_register(hkcp_t *hkcp, char *name, int local, int event)
{
	hkcp_source_t *source;

	/* Ensure there is no name conflict against sink and source namespaces */
	if (hkcp_sink_retrieve(hkcp, name)) {
		log_str("ERROR: Cannot register source '%s': a sink is already registered with this name\n", name);
		return -1;
	}

	if (hkcp_source_retrieve(hkcp, name) != NULL) {
		log_str("ERROR: Cannot register sink '%s': a source is already registered with this name\n", name);
		return -1;
	}

	/* Allocate new source */
	source = hkcp_source_alloc(hkcp);
	source->ep.name = strdup(name);
	log_debug(2, "hkcp_source_register name='%s' (%d elements)", name, hkcp->sources.nmemb);

	hkcp_source_set_widget_name(source, NULL);
	buf_set_str(&source->ep.value, "");

	if (local) {
		source->ep.flag |= HKCP_FLAG_LOCAL;
	}

	if (event) {
		source->ep.flag |= HKCP_FLAG_EVENT;
	}

	/* Trigger advertising */
	if (!local) {
		hk_advertise_source(&hkcp->adv);
	}

	return source->ep.id;
}


void hkcp_source_set_widget(hkcp_t *hkcp, int id, char *widget_name)
{
	hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, id);
	if (source != NULL) {
		hkcp_source_set_widget_name(source, widget_name);
	}
	else {
		log_str("PANIC: Attempting to set widget on unknown source #%d\n", id);
	}
}


#if 0
void hkcp_source_unregister(hkcp_t *hkcp, char *name)
{
	hkcp_source_t *source = hkcp_source_retrieve(hkcp, name);

	log_debug(2, "hkcp_source_unregister name='%s'", name);

	if (source != NULL) {
		free(source->name);
		buf_cleanup(&source->value);
		source->flag = 0;
		hk_tab_cleanup(&source->nodes);
	}
}
#endif


static int hkcp_source_node_attached(hkcp_source_t *source, hkcp_node_t *node)
{
	int i;

	if (source->ep.name == NULL) {
		return 0;
	}

	for (i = 0; i < source->nodes.nmemb; i++) {
		hkcp_node_t *node2 = HK_TAB_VALUE(source->nodes, hkcp_node_t *, i);
		if (node2 == node) {
			return 1;
		}
	}

	return 0;
}


static void hkcp_source_attach_node(hkcp_source_t *source, hkcp_node_t *node)
{
	int i;

	if (hkcp_source_node_attached(source, node)) {
		return;
	}

	for (i = 0; i < source->nodes.nmemb; i++) {
		hkcp_node_t **pnode = HK_TAB_PTR(source->nodes, hkcp_node_t *, i);
		if (*pnode == NULL) {
			*pnode = node;
			return;
		}
	}

	hkcp_node_t **pnode = hk_tab_push(&source->nodes);
	*pnode = node;

	log_debug(2, "hkcp_source_attach_node source='%s' node=#%d='%s' (%d elements)", source->ep.name, node->id, node->name, source->nodes.nmemb);
}


static void hkcp_source_send_initial_value(hkcp_source_t *source, hkcp_node_t *node)
{
	log_debug(3, "hkcp_source_send_initial_value source='%s' flag=%02X node=#%d='%s'", source->ep.name, source->ep.flag, node->id, node->name);

	/* Do not send initial value if source is declared as an event */
	if (source->ep.flag & HKCP_FLAG_EVENT) {
		return;
	}

	/* Send initial value if node is attached to source */
	if (hkcp_source_node_attached(source, node)) {
		int size = strlen(source->ep.name) + source->ep.value.len + 10;
		char str[size];
		int len;

		len = snprintf(str, size-1, "set %s=%s", source->ep.name, source->ep.value.base);
		log_debug(2, "hkcp_source_send_initial_value cmd='%s' node=#%d='%s'", str, node->id, node->name);
		str[len++] = '\n';

		tcp_sock_write(&node->tcp_sock, str, len);
	}
}


static int hkcp_source_send_watch(tcp_sock_t *tcp_sock, char *str)
{
	hkcp_command_ctx_t *ctx = tcp_sock_get_data(tcp_sock);

	if (ctx->watch) {
		tcp_sock_write(tcp_sock, str, strlen(str));
	}

	return 1;
}


char *hkcp_source_update(hkcp_t *hkcp, int id, char *value)
{
	hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, id);

	if ((source == NULL) || (source->ep.name == NULL)) {
		log_str("PANIC: Attempting to update unknown source #%d\n", id);
		return NULL;
	}

	log_debug(3, "hkcp_source_update id=%d name='%s' value='%s'", id, source->ep.name, value);

	buf_set_str(&source->ep.value, value);

	if ((source->ep.flag & HKCP_FLAG_LOCAL) == 0) {
		int size = strlen(source->ep.name) + source->ep.value.len + 20;
		char str[size];
		int len;
		int i;

		/* Setup command to send */
		len = snprintf(str, size-1, "set %s=%s", source->ep.name, source->ep.value.base);
		log_debug(2, "hkcp_source_send cmd='%s' (%d nodes)", str, source->nodes.nmemb);
		str[len++] = '\n';

		/* Send to all nodes that subscribed this source */
		for (i = 0; i < source->nodes.nmemb; i++) {
			hkcp_node_t *node = HK_TAB_VALUE(source->nodes, hkcp_node_t *, i);
			if (node != NULL) {
				log_debug(2, "  node=#%d='%s'", node->id, node->name);
				tcp_sock_write(&node->tcp_sock, str, len);
			}
		}

		/* Send event to watchers */
		snprintf(str, size, "!%s=%s\n", source->ep.name, source->ep.value.base);
		tcp_srv_foreach_client(&hkcp->tcp_srv, (tcp_foreach_func_t) hkcp_source_send_watch, str);
	}

	return source->ep.name;
}


static int hkcp_source_is_(hkcp_t *hkcp, int id, unsigned int mask)
{
	hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, id);

	if (source == NULL) {
		log_str("PANIC: Attempting to get flag on unknown source #%d\n", id);
		return 0;
	}

	return (source->ep.flag & mask) ? 1:0;
}


int hkcp_source_is_local(hkcp_t *hkcp, int id)
{
	return hkcp_source_is_(hkcp, id, HKCP_FLAG_LOCAL);
}


int hkcp_source_is_event(hkcp_t *hkcp, int id)
{
	return hkcp_source_is_(hkcp, id, HKCP_FLAG_EVENT);
}


/*
 * TCP/stdin/websocket commands
 */

static int hkcp_command_tcp(hkcp_t *hkcp, int argc, char **argv, tcp_sock_t *tcp_sock);

static void hkcp_command_ctx_recv(hkcp_command_ctx_t *ctx, int argc, char **argv)
{
	hkcp_command_tcp(ctx->hkcp, argc, argv, ctx->tcp_sock);
}


static hkcp_command_ctx_t *hkcp_command_ctx_new(tcp_sock_t *tcp_sock)
{
	hkcp_command_ctx_t *ctx;

	ctx = (hkcp_command_ctx_t *) malloc(sizeof(hkcp_command_ctx_t));
	ctx->hkcp = tcp_sock_get_data(tcp_sock);
	ctx->tcp_sock = tcp_sock;
	ctx->cmd = command_new((command_handler_t) hkcp_command_ctx_recv, ctx);
	ctx->watch = 0;

	return ctx;
}


static void hkcp_command_ctx_destroy(hkcp_command_ctx_t *ctx)
{
	command_destroy(ctx->cmd);
	memset(ctx, 0, sizeof(hkcp_command_ctx_t));
	free(ctx);
}


static void hkcp_command_set(hkcp_t *hkcp, int argc, char **argv, buf_t *out_buf)
{
	int i;

	for (i = 1; i < argc; i++) {
		char *args = argv[i];
		char *value = strchr(args, '=');

		if (value != NULL) {
			hkcp_sink_t *sink;

			*(value++) = '\0';
			sink = hkcp_sink_retrieve(hkcp, args);
			if (sink != NULL) {
				/* Update sink value and invoke sink event callback */
				hkcp_sink_update_(sink, value);
			}
			else {
				/* Send back error message */
				buf_append_str(out_buf, ".ERROR: Unknown sink: ");
				buf_append_str(out_buf, args);
				buf_append_str(out_buf, "\n");
			}
		}
		else {
			buf_append_str(out_buf, ".ERROR: Syntax error in command: ");
			buf_append_str(out_buf, args);
			buf_append_str(out_buf, "\n");
		}
	}
}


static void hkcp_command_get(hkcp_t *hkcp, int argc, char **argv, buf_t *out_buf)
{
	int i;

	if (argc > 1) {
		for (i = 1; i < argc; i++) {
			hkcp_source_t *source = hkcp_source_retrieve(hkcp, argv[i]);
			if (source != NULL) {
				hkcp_ep_dump(HKCP_EP(source), out_buf);
			}
		}
		for (i = 1; i < argc; i++) {
			hkcp_sink_t *sink = hkcp_sink_retrieve(hkcp, argv[i]);
			if (sink != NULL) {
				hkcp_ep_dump(HKCP_EP(sink), out_buf);
			}
		}
	}
	else {
		for (i = 0; i < hkcp->sources.nmemb; i++) {
			hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
			if (source->ep.name != NULL) {
				hkcp_ep_dump(HKCP_EP(source), out_buf);
			}
		}
		for (i = 0; i < hkcp->sinks.nmemb; i++) {
			hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, i);
			if (sink->ep.name != NULL) {
				hkcp_ep_dump(HKCP_EP(sink), out_buf);
			}
		}
	}

	buf_append_str(out_buf, ".\n");
}


static void hkcp_command_nodes(hkcp_t *hkcp, buf_t *out_buf)
{
	int i, j, k;

	for (i = 0; i < hkcp->nodes.nmemb; i++) {
		hkcp_node_t *node = HK_TAB_VALUE(hkcp->nodes, hkcp_node_t *, i);

		if (node->name != NULL) {
			buf_append_str(out_buf, node->name);

			for (j = 0; j < hkcp->sources.nmemb; j++) {
				hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, j);

				if (source->ep.name != NULL) {
					for (k = 0; k < source->nodes.nmemb; k++) {
						hkcp_node_t *node2 = HK_TAB_VALUE(source->nodes, hkcp_node_t *, k);
						if (node2 == node) {
							buf_append_str(out_buf, " ");
							hkcp_ep_append_name(HKCP_EP(source), out_buf);
						}
					}
				}
			}

			buf_append_str(out_buf, "\n");
		}
	}

	buf_append_str(out_buf, ".\n");
}


static void hkcp_command_sources(hkcp_t *hkcp, buf_t *out_buf)
{
	int i, j;

	for (i = 0; i < hkcp->sources.nmemb; i++) {
		hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);

		if ((source->ep.name != NULL) && ((source->ep.flag & HKCP_FLAG_LOCAL) == 0)) {
			hkcp_ep_append_name(HKCP_EP(source), out_buf);

			buf_append_str(out_buf, " \"");
			buf_append(out_buf, source->ep.value.base, source->ep.value.len);
			buf_append_str(out_buf, "\"");

			for (j = 0; j < source->nodes.nmemb; j++) {
				hkcp_node_t *node = HK_TAB_VALUE(source->nodes, hkcp_node_t *, j);
				if (node != NULL) {
					buf_append_str(out_buf, " ");
					buf_append_str(out_buf, node->name);
				}
			}

			buf_append_str(out_buf, "\n");
		}
	}

	buf_append_str(out_buf, ".\n");
}


static void hkcp_command_sinks(hkcp_t *hkcp, buf_t *out_buf)
{
	int i;

	for (i = 0; i < hkcp->sinks.nmemb; i++) {
		hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, i);

		if ((sink->ep.name != NULL) && ((sink->ep.flag & HKCP_FLAG_LOCAL) == 0)) {
			hkcp_ep_append_name(HKCP_EP(sink), out_buf);
			buf_append_str(out_buf, " \"");
			buf_append(out_buf, sink->ep.value.base, sink->ep.value.len);
			buf_append_str(out_buf, "\"\n");
		}
	}

	buf_append_str(out_buf, ".\n");
}


static void hkcp_command_watch(hkcp_t *hkcp, int argc, char **argv, buf_t *out_buf, hkcp_command_ctx_t *ctx)
{
	int show = 1;
	int err = 0;

	if (argc > 1) {
		if (argc == 2) {
			if ((strcmp(argv[1], "0") == 0) || (strcmp(argv[1], "off") == 0)) {
				ctx->watch = 0;
				show = 0;
			}
			else if ((strcmp(argv[1], "1") == 0) || (strcmp(argv[1], "on") == 0)) {
				ctx->watch = 1;
			}
			else {
				err = 1;
			}
		}
		else {
			err = 1;
		}
	}

	if (err) {
		buf_append_str(out_buf, ".ERROR: watch: Syntax error");
	}
	else if (show) {
		int i;

		for (i = 0; i < hkcp->sources.nmemb; i++) {
			hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
			if (source->ep.name != NULL) {
				buf_append_str(out_buf, "!");
				buf_append_str(out_buf, source->ep.name);
				buf_append_str(out_buf, "=");
				buf_append(out_buf, source->ep.value.base, source->ep.value.len);
				buf_append_str(out_buf, "\n");
			}
		}
	}
}


void hkcp_command(hkcp_t *hkcp, int argc, char **argv, buf_t *out_buf)
{
	int i;

	if (strcmp(argv[0], "set") == 0) {
		hkcp_command_set(hkcp, argc, argv, out_buf);
	}
	else if (strcmp(argv[0], "get") == 0) {
		hkcp_command_get(hkcp, argc, argv, out_buf);
	}
	else if (strcmp(argv[0], "nodes") == 0) {
		hkcp_command_nodes(hkcp, out_buf);
	}
	else if (strcmp(argv[0], "sinks") == 0) {
		hkcp_command_sinks(hkcp, out_buf);
	}
	else if (strcmp(argv[0], "sources") == 0) {
		hkcp_command_sources(hkcp, out_buf);
	}
	else if (strcmp(argv[0], "echo") == 0) {
		for (i = 1; i < argc; i++) {
			if (i > 1) {
				buf_append_str(out_buf, " ");
			}
			buf_append_str(out_buf, argv[i]);
		}
		buf_append_str(out_buf, "\n.\n");
	}
	else if (strcmp(argv[0], "version") == 0) {
		buf_append_str(out_buf, HAKIT_VERSION " " ARCH "\n.\n");
	}
	else {
		buf_append_str(out_buf, ".ERROR: Unknown command: ");
		buf_append_str(out_buf, argv[0]);
		buf_append_str(out_buf, "\n");
	}
}


static int hkcp_command_tcp(hkcp_t *hkcp, int argc, char **argv, tcp_sock_t *tcp_sock)
{
	buf_t out_buf;

	if (argc <= 0) {
		return 0;
	}

	if (argv[0][0] == '.') {
		return 1;
	}

	buf_init(&out_buf);

	if (strcmp(argv[0], "watch") == 0) {
		hkcp_command_ctx_t *ctx = tcp_sock_get_data(tcp_sock);
		hkcp_command_watch(hkcp, argc, argv, &out_buf, ctx);
	}
	else {
		hkcp_command(hkcp, argc, argv, &out_buf);
	}

	io_channel_write(&tcp_sock->chan, (char *) out_buf.base, out_buf.len);

	buf_cleanup(&out_buf);

	return 0;
}


static void hkcp_tcp_event(tcp_sock_t *tcp_sock, tcp_io_t io, char *rbuf, int rsize)
{
	hkcp_command_ctx_t *ctx;

	log_debug(2, "hkcp_tcp_event [%d]", tcp_sock->chan.fd);
	log_debug_data((unsigned char *) rbuf, rsize);

	switch (io) {
	case TCP_IO_CONNECT:
		log_debug(2, "  CONNECT %s", rbuf);
		ctx = hkcp_command_ctx_new(tcp_sock);
		tcp_sock_set_data(tcp_sock, ctx);
		break;
	case TCP_IO_DATA:
		log_debug(2, "  DATA %d", rsize);
		ctx = tcp_sock_get_data(tcp_sock);
		command_recv(ctx->cmd, rbuf, rsize);
		break;
	case TCP_IO_HUP:
		log_debug(2, "  HUP");
		ctx = tcp_sock_get_data(tcp_sock);
		hkcp_command_ctx_destroy(ctx);
		break;
	default:
		log_str("  PANIC: unknown event caught");
		break;
	}
}


/*
 * Management engine
 */

static void hkcp_discovered(hkcp_t *hkcp, char *remote_ip)
{
	log_debug(2, "hkcp_discovered %s", remote_ip);

	/* Add this node if we have some sources to export */
	if (hkcp_source_to_advertise(hkcp)) {
		/* Create node, if it does not already exist */
		hkcp_node_add(hkcp, remote_ip);
	}
}


int hkcp_init(hkcp_t *hkcp, int port)
{
	int ret = -1;

	memset(hkcp, 0, sizeof(hkcp_t));
	hkcp->port = port;
	tcp_srv_clear(&hkcp->tcp_srv);
	hk_tab_init(&hkcp->nodes, sizeof(hkcp_node_t *));
	hk_tab_init(&hkcp->sinks, sizeof(hkcp_sink_t));
	hk_tab_init(&hkcp->sources, sizeof(hkcp_source_t));

	if (port > 0) {
		if (hk_advertise_init(&hkcp->adv, port)) {
			goto DONE;
		}

		hk_advertise_handler(&hkcp->adv, DISCOVER_HKCP, (hk_advertise_func_t) hkcp_discovered, hkcp);

		if (tcp_srv_init(&hkcp->tcp_srv, port, hkcp_tcp_event, hkcp)) {
			goto DONE;
		}
	}

	ret = 0;

DONE:
	if (ret < 0) {
		hk_advertise_shutdown(&hkcp->adv);

		if (hkcp->tcp_srv.csock.chan.fd > 0) {
			tcp_srv_shutdown(&hkcp->tcp_srv);
		}

		memset(hkcp, 0, sizeof(hkcp_t));
	}

	return ret;
}
