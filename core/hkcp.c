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
#include "options.h"
#include "buf.h"
#include "iputils.h"
#include "netif.h"
#include "tcpio.h"
#include "udpio.h"
#include "command.h"
#include "history.h"
#include "hkcp.h"


#define INTERFACE_CHECK_DELAY 60000   // Check for available network interfaces once per minute

#define ADVERTISE_DELAY 1000   // Delay before advertising a newly registered sink/source
#define ADVERTISE_MAXLEN 1200  // Maxim advertising packet length

#define UDP_SIGN        0xAC   // Advertising packet signature
#define UDP_TYPE_SINK   0x01   // Advertising field type: sink
#define UDP_TYPE_SOURCE 0x02   // Advertising field type: source
#define UDP_TYPE_MONITOR 0x03  // Advertising field type: monitor


/* Local functions forward declarations */
static void hkcp_udp_send(hkcp_t *hkcp, buf_t *buf, int reply);
static void hkcp_advertise(hkcp_t *hkcp);
static void hkcp_command_tcp(hkcp_t *hkcp, int argc, char **argv, tcp_sock_t *tcp_sock);
static int hkcp_node_connect(hkcp_node_t *node);
static hkcp_source_t *hkcp_source_retrieve(hkcp_t *hkcp, char *name);
static void hkcp_source_send_initial_value(hkcp_source_t *source, hkcp_node_t *node);



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


static void hkcp_node_command(hkcp_node_t *node, int argc, char **argv)
{
	hkcp_command_tcp(node->hkcp, argc, argv, &node->tcp_sock);
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
	node->cmd = command_new((command_handler_t) hkcp_node_command, node);

	return node;
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
		command_recv(node->cmd, rbuf, rsize);
		break;
	case TCP_IO_HUP:
		log_debug(2, "  HUP");

		/* Clear command context */
		command_clear(node->cmd);

		/* Try to reconnect immediately */
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

	/* Clear command buffering context */
	command_clear(node->cmd);

	/* Free node name */
	free(node->name);
	node->name = NULL;

	/* Free node descriptor */
	memset(node, 0, sizeof(hkcp_node_t));
	free(node);
}


static void hkcp_node_send_initial_values(hkcp_node_t *node)
{
	hkcp_t *hkcp = node->hkcp;
	int i;

	for (i = 0; i < hkcp->sources.nmemb; i++) {
		hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
		hkcp_source_send_initial_value(source, node);
	}
}


static int hkcp_node_connect(hkcp_node_t *node)
{
	node->connect_attempts++;

	log_str("Connecting to node #%d='%s' (%d/4)", node->id, node->name, node->connect_attempts);
	if (tcp_sock_connect(&node->tcp_sock, node->name, node->hkcp->udp_srv.port, hkcp_node_event, node) > 0) {
		node->timeout_tag = 0;
		hkcp_node_send_initial_values(node);
		return 0;
	}

	if (node->connect_attempts > 4) {
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


static void hkcp_ep_set_local(hkcp_ep_t *ep)
{
	if (ep->name != NULL) {
		ep->flag |= HKCP_FLAG_LOCAL;
	}
	else {
		log_str("PANIC: Attempting to set LOCAL flag on unknown %s #%d\n", hkcp_ep_type_str(ep), ep->id);
	}
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
	if (ep->flag & HKCP_FLAG_MONITOR) {
		buf_append_str(out_buf, "(");
	}
	buf_append_str(out_buf, ep->name);
	if (ep->flag & HKCP_FLAG_MONITOR) {
		buf_append_str(out_buf, ")");
	}
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

static void hkcp_sink_advertise(hkcp_t *hkcp, int reply)
{
	int nsinks = hkcp->sinks.nmemb;
	buf_t buf;
	int i;

	// Nothing to do in local mode
	if (hkcp->udp_srv.chan.fd > 0) {
		return;
	}

	// Nothing to do if no sink registered
	if (nsinks <= 0) {
		return;
	}

	log_str("Advertising %d sink%s as %s", nsinks, (nsinks > 1) ? "s":"", reply ? "reply":"broadcast");

	buf_init(&buf);

	for (i = 0; i < nsinks; i++) {
		hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, i);
		if ((sink->ep.name != NULL) && ((sink->ep.flag & (HKCP_FLAG_MONITOR|HKCP_FLAG_LOCAL)) == 0)) {
			if (buf.len == 0) {
				buf_append_byte(&buf, UDP_SIGN);
				buf_append_byte(&buf, UDP_TYPE_SINK);
			}
			buf_append_str(&buf, sink->ep.name);
			buf_append_byte(&buf, 0);

			if (buf.len > ADVERTISE_MAXLEN) {
				hkcp_udp_send(hkcp, &buf, reply);
			}
		}
	}

	if (buf.len > 0) {
		hkcp_udp_send(hkcp, &buf, reply);
	}

	buf_cleanup(&buf);
}


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
	hkcp_sink_t *sink = NULL;
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


int hkcp_sink_register(hkcp_t *hkcp, char *name)
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

	/* Trigger advertising */
	hkcp_advertise(hkcp);

	/* Declare new sink in history */
	history_signal_declare(-(sink->ep.id+1), name);

	return sink->ep.id;
}


void hkcp_sink_set_local(hkcp_t *hkcp, int id)
{
	hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, id);
	if (sink != NULL) {
		hkcp_ep_set_local(HKCP_EP(sink));
	}
	else {
		log_str("PANIC: Attempting to set local flag on unknown sink #%d\n", id);
	}
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


static hkcp_sink_t *hkcp_sink_monitor(hkcp_t *hkcp, char *name)
{
	hkcp_sink_t *sink;

	sink = hkcp_sink_alloc(hkcp);
	sink->ep.name = strdup(name);

	log_debug(2, "hkcp_sink_monitor name='%s' (%d elements)", name, hkcp->sinks.nmemb);

	buf_set_str(&sink->ep.value, "");
	sink->ep.flag |= HKCP_FLAG_MONITOR;

	hk_tab_init(&sink->handlers, sizeof(hkcp_sink_handler_t));

	return sink;
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
	/* Update sink value */
	buf_set_str(&sink->ep.value, value);

	/* Invoke sink event callback */
	if ((sink->ep.flag & HKCP_FLAG_MONITOR) == 0) {
		int i;

		for (i = 0; i < sink->handlers.nmemb; i++) {
			hkcp_sink_handler_t *handler = HK_TAB_PTR(sink->handlers, hkcp_sink_handler_t, i);
			if (handler->func != NULL) {
				handler->func(handler->user_data, sink->ep.name, (char *) sink->ep.value.base);
			}
		}
	}

	history_feed(-(sink->ep.id+1), value);

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


/*
 * Sources
 */

static void hkcp_source_advertise(hkcp_t *hkcp, int reply)
{
	int nsources = hkcp->sources.nmemb;
	buf_t buf;
	int i;

	// Nothing to do in local mode
	if (hkcp->udp_srv.chan.fd > 0) {
		return;
	}

	// Nothing to do if no source registered
	if (nsources <= 0) {
		return;
	}

	log_str("Advertising %d source%s as %s", nsources, (nsources > 1) ? "s":"", reply ? "reply":"broadcast");

	buf_init(&buf);

	for (i = 0; i < nsources; i++) {
		hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
		if ((source->ep.name != NULL) && ((source->ep.flag & HKCP_FLAG_MONITOR) == 0)) {
			if (buf.len == 0) {
				buf_append_byte(&buf, UDP_SIGN);
				buf_append_byte(&buf, UDP_TYPE_SOURCE);
			}
			buf_append_str(&buf, source->ep.name);
			buf_append_byte(&buf, 0);

			if (buf.len > ADVERTISE_MAXLEN) {
				hkcp_udp_send(hkcp, &buf, reply);
			}
		}
	}

	if (buf.len > 0) {
		hkcp_udp_send(hkcp, &buf, reply);
	}

	buf_cleanup(&buf);
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


int hkcp_source_register(hkcp_t *hkcp, char *name, int event)
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

	if (event) {
		source->ep.flag |= HKCP_FLAG_EVENT;
	}

	/* Trigger advertising */
	hkcp_advertise(hkcp);

	/* Declare new source in history */
	history_signal_declare(source->ep.id+1, name);

	return source->ep.id;
}


void hkcp_source_set_local(hkcp_t *hkcp, int id)
{
	hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, id);
	if (source != NULL) {
		hkcp_ep_set_local(HKCP_EP(source));
	}
	else {
		log_str("PANIC: Attempting to set local flag on unknown source #%d\n", id);
	}
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


static hkcp_source_t *hkcp_source_monitor(hkcp_t *hkcp, char *name)
{
	hkcp_source_t *source;

	source = hkcp_source_alloc(hkcp);
	source->ep.name = strdup(name);

	log_debug(2, "hkcp_source_monitor name='%s' (%d elements)", name, hkcp->sources.nmemb);

	buf_set_str(&source->ep.value, "");
	source->ep.flag |= HKCP_FLAG_MONITOR;

	return source;
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

	/* Do not send initial value if source is declared as an event or as a monitored source */
	if (source->ep.flag & (HKCP_FLAG_EVENT | HKCP_FLAG_MONITOR)) {
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


static void hkcp_source_send(hkcp_source_t *source)
{
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
	}
}


char *hkcp_source_update(hkcp_t *hkcp, int id, char *value)
{
	hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, id);

	if ((source == NULL) || (source->ep.name == NULL)) {
		log_str("PANIC: Attempting to update unknown source #%d\n", id);
		return NULL;
	}

	buf_set_str(&source->ep.value, value);
	hkcp_source_send(source);

	history_feed(source->ep.id+1, value);

	return source->ep.name;
}


/*
 * UDP Commands
 */

static void hkcp_udp_send(hkcp_t *hkcp, buf_t *buf, int reply)
{
	int i;

	// Nothing to do in local mode
	if (hkcp->udp_srv.chan.fd > 0) {
		return;
	}

	if (reply) {
		udp_srv_send_reply(&hkcp->udp_srv, (char *) buf->base, buf->len);
	}
	else {
		/* Send UDP packet as broadcast */
		udp_srv_send_bcast(&hkcp->udp_srv, (char *) buf->base, buf->len);

		/* Also send UDP packet to hosts provides with command option --hosts */
		for (i = 0; i < hkcp->hosts.nmemb; i++) {
			char *host = HK_TAB_VALUE(hkcp->hosts, char *, i);
			udp_srv_send_to(&hkcp->udp_srv, (char *) buf->base, buf->len, host);
		}
	}

	buf->len = 0;
}


static void hkcp_udp_event_sink(hkcp_t *hkcp, int argc, char **argv)
{
	int i;

	log_debug(2, "hkcp_udp_event_sink (%d sinks)", argc);

	/* Check for local sources matching advertised sinks */
	for (i = 0; i < argc; i++) {
		char *sink_name = argv[i];
		hkcp_source_t *source = hkcp_source_retrieve(hkcp, sink_name);

		/* Special processing for monitor mode:
		   If no source is registered matching the advertised sink,
		   we automatically register this source as an event,
		   so that it will be possible to update the remote sink */
		if (hkcp->monitor.func != NULL) {
			if (source == NULL) {
				source = hkcp_source_monitor(hkcp, sink_name);
			}
		}

		/* If matching source is found, check for requesting node connection */
		if (source != NULL) {
			log_debug(2, "  remote sink='%s', local source='%s'", sink_name, source->ep.name);

			if ((source->ep.flag & HKCP_FLAG_LOCAL) == 0) {
				struct sockaddr_in *addr = udp_srv_remote(&hkcp->udp_srv);
				unsigned long addr_v = ntohl(addr->sin_addr.s_addr);
				char name[32];
				hkcp_node_t *node;

				/* Get remote IP address as node name */
				snprintf(name, sizeof(name), "%lu.%lu.%lu.%lu",
					 (addr_v >> 24) & 0xFF, (addr_v >> 16) & 0xFF, (addr_v >> 8) & 0xFF, addr_v & 0xFF);

				/* Connect to node (if not already done) */
				node = hkcp_node_add(hkcp, name);

				/* Attach source to node (if not already done) */
				hkcp_source_attach_node(source, node);
			}
		}
		else {
			log_debug(2, "  remote sink='%s', no local source", sink_name);
		}
	}
}


static void hkcp_udp_event_source(hkcp_t *hkcp, int argc, char **argv)
{
	int found = 0;
	int i;

	log_debug(2, "hkcp_udp_event_source (%d sources)", argc);

	/* Check for local sinks matching advertised sources */
	for (i = 0; i < argc; i++) {
		char *source_name = argv[i];
		hkcp_sink_t *sink = hkcp_sink_retrieve(hkcp, source_name);

		/* Check for non-local sinks matching sources in request list */
		if ((sink != NULL) && ((sink->ep.flag & HKCP_FLAG_LOCAL) == 0)) {
			found = 1;
			break;
		}
	}

	/* If matching source found, send back sink advertisement
	   as a reply to the requesting node */
	if (found) {
		log_debug(2, "  Matching sink found");
		hkcp_sink_advertise(hkcp, 1);
	}
	else {
		log_debug(2, "  No matching sink found");
	}
}


static void hkcp_udp_event(hkcp_t *hkcp, unsigned char *buf, int size)
{
	int argc = 0;
	char **argv = NULL;
	int i;
	int nl;

	log_debug(2, "hkcp_udp_event: %d bytes", size);
	log_debug_data(buf, size);

	if (size < 2) {
		log_str("WARNING: Received too short UDP packet");
		return;
	}

	if (buf[0] != UDP_SIGN) {
		log_str("WARNING: Received UDP packet with wrong magic number");
		return;
	}

	/* Construct a list of received elements */
	for (i = 2; i < size; i++) {
		if (buf[i] == 0) {
			argc++;
		}
	}

	if (argc > 0) {
		argv = (char **) malloc(argc*sizeof(char *));
		argc = 0;
		nl = 1;
		for (i = 2; i < size; i++) {
			if (nl) {
				argv[argc++] = (char *) &buf[i];
				nl = 0;
			}
			if (buf[i] == 0) {
				nl = 1;
			}
		}
	}

	switch (buf[1]) {
	case UDP_TYPE_SINK:
		hkcp_udp_event_sink(hkcp, argc, argv);
		break;
	case UDP_TYPE_SOURCE:
		/* If monitor mode enabled, tell sender we need to receive all sources events from it */
		if (hkcp->monitor.func != NULL) {
			for (i = 0; i < argc; i++) {
				char *source_name = argv[i];
				hkcp_sink_t *sink = hkcp_sink_retrieve(hkcp, source_name);

				/* If no sink is registered matching the advertised source,
				   we automatically register this sink,
				   so that it will be updated from remote sources */
				if (sink == NULL) {
					hkcp_sink_monitor(hkcp, source_name);
				}

				hkcp->monitor.func(hkcp, source_name, NULL);
			}

			buf[1] = UDP_TYPE_SINK;
			udp_srv_send_reply(&hkcp->udp_srv, (char *) buf, size);
		}
		else {
			hkcp_udp_event_source(hkcp, argc, argv);
		}
		break;
	case UDP_TYPE_MONITOR:
		hkcp_sink_advertise(hkcp, 1);
		hkcp_source_advertise(hkcp, 1);
		break;
	default:
		log_str("WARNING: Received UDP packet with unknown type (%02X)", buf[1]);
		break;
	}

	free(argv);
	argv = NULL;
}


/*
 * TCP/stdin/websocket commands
 */

typedef struct {
	hkcp_t *hkcp;
	tcp_sock_t *tcp_sock;
	command_t *cmd;
} hkcp_command_ctx_t;


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
				/* Send back error message if not in monitor mode */
				if (hkcp->monitor.func == NULL) {
					buf_append_str(out_buf, "ERROR: Unknown sink: ");
					buf_append_str(out_buf, args);
					buf_append_str(out_buf, "\n");
				}
			}

			/* Invoke monitoring callback */
			if (hkcp->monitor.func != NULL) {
				hkcp->monitor.func(hkcp->monitor.user_data, args, value);
			}
		}
		else {
			buf_append_str(out_buf, "ERROR: Syntax error in command: ");
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
}


static void hkcp_command_status(hkcp_t *hkcp, buf_t *out_buf)
{
	int i;

	buf_append_fmt(out_buf, "Nodes: %d\n", hkcp->nodes.nmemb);

	for (i = 0; i < hkcp->nodes.nmemb; i++) {
		hkcp_node_t *node = HK_TAB_VALUE(hkcp->nodes, hkcp_node_t *, i);
		int j;

		if (node->name != NULL) {
			buf_append_str(out_buf, "  ");
			buf_append_str(out_buf, node->name);

			for (j = 0; j < hkcp->sources.nmemb; j++) {
				hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, j);
				int k;

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

	buf_append_fmt(out_buf, "Sources: %d\n", hkcp->sources.nmemb);
	for (i = 0; i < hkcp->sources.nmemb; i++) {
		hkcp_source_t *source = HK_TAB_PTR(hkcp->sources, hkcp_source_t, i);
		int j;

		if (source->ep.name != NULL) {
			buf_append_str(out_buf, "  ");
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

	buf_append_fmt(out_buf, "Sinks: %d\n", hkcp->sinks.nmemb);
	for (i = 0; i < hkcp->sinks.nmemb; i++) {
		hkcp_sink_t *sink = HK_TAB_PTR(hkcp->sinks, hkcp_sink_t, i);

		if (sink->ep.name != NULL) {
			buf_append_str(out_buf, "  ");
			hkcp_ep_append_name(HKCP_EP(sink), out_buf);

			buf_append_str(out_buf, " \"");
			buf_append(out_buf, sink->ep.value.base, sink->ep.value.len);
			buf_append_str(out_buf, "\"");

			buf_append_str(out_buf, "\n");
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
	else if (strcmp(argv[0], "status") == 0) {
		hkcp_command_status(hkcp, out_buf);
	}
	else if (strcmp(argv[0], "echo") == 0) {
		for (i = 1; i < argc; i++) {
			if (i > 1) {
				buf_append_str(out_buf, " ");
			}
			buf_append_str(out_buf, argv[i]);
		}
		buf_append_str(out_buf, "\n");
	}
	else {
		buf_append_str(out_buf, "ERROR: Unknown command: ");
		buf_append_str(out_buf, argv[0]);
		buf_append_str(out_buf, "\n");
	}
}


static void hkcp_command_tcp(hkcp_t *hkcp, int argc, char **argv, tcp_sock_t *tcp_sock)
{
	buf_t out_buf;

	if (argc > 0) {
		if (strcmp(argv[0], "ERROR:") == 0) {
			return;
		}

		buf_init(&out_buf);

		hkcp_command(hkcp, argc, argv, &out_buf);
		io_channel_write(&tcp_sock->chan, (char *) out_buf.base, out_buf.len);

		buf_cleanup(&out_buf);
	}
}


static void hkcp_command_stdin(hkcp_t *hkcp, int argc, char **argv)
{
	buf_t out_buf;

	if (argc > 0) {
		buf_init(&out_buf);

		hkcp_command(hkcp, argc, argv, &out_buf);
		if (fwrite(out_buf.base, 1, out_buf.len, stdout) < 0) {
			log_str("PANIC: Failed to write stdout: %s", strerror(errno));
		}

		buf_cleanup(&out_buf);
	}
	else {
		/* Quit if hangup from stdin */
		sys_quit();
	}
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

static void hkcp_monitor_advertise(hkcp_t *hkcp)
{
	buf_t buf;

	log_debug(2, "Advertising monitor mode");

	buf_init(&buf);
	buf_append_byte(&buf, UDP_SIGN);
	buf_append_byte(&buf, UDP_TYPE_MONITOR);
	hkcp_udp_send(hkcp, &buf, 0);
	buf_cleanup(&buf);
}


static int hkcp_advertise_now(hkcp_t *hkcp)
{
	hkcp->advertise_tag = 0;
	hkcp_sink_advertise(hkcp, 0);
	hkcp_source_advertise(hkcp, 0);

	if (hkcp->monitor.func != NULL) {
		hkcp_monitor_advertise(hkcp);
	}

	return 0;
}


static void hkcp_advertise(hkcp_t *hkcp)
{
	if (hkcp->advertise_tag != 0) {
		sys_remove(hkcp->advertise_tag);
		hkcp->advertise_tag = 0;
	}

	if (hkcp->udp_srv.chan.fd > 0) {
		log_debug(2, "Will send advertisement request in %lu ms", ADVERTISE_DELAY);
		hkcp->advertise_tag = sys_timeout(ADVERTISE_DELAY, (sys_func_t) hkcp_advertise_now, hkcp);
	}
}


static int hkcp_check_interfaces(hkcp_t *hkcp)
{
	int ninterfaces = netif_check_interfaces();

	if (ninterfaces != hkcp->ninterfaces) {
		hkcp->ninterfaces = ninterfaces;

		log_str("Network interface change detected");
		netif_show_interfaces();

		if (ninterfaces > 0) {
			hkcp_advertise(hkcp);
		}
	}

	return 1;
}


static void hkcp_init_hosts(hkcp_t *hkcp)
{
	char *s1 = opt_hosts;

	while ((s1 != NULL) && (*s1 != '\0')) {
		char *s2 = s1;
		while ((*s2 != '\0') && (*s2 != ',')) {
			s2++;
		}
		if (*s2 != '\0') {
			*(s2++) = '\0';
		}

		char **s = hk_tab_push(&hkcp->hosts);
		*s = s1;

		s1 = s2;
	}
}


int hkcp_init(hkcp_t *hkcp, int port)
{
	int ret = -1;

	memset(hkcp, 0, sizeof(hkcp_t));
	udp_srv_clear(&hkcp->udp_srv);
	tcp_srv_clear(&hkcp->tcp_srv);
	hk_tab_init(&hkcp->hosts, sizeof(char *));
	hk_tab_init(&hkcp->nodes, sizeof(hkcp_node_t *));
	hk_tab_init(&hkcp->sinks, sizeof(hkcp_sink_t));
	hk_tab_init(&hkcp->sources, sizeof(hkcp_source_t));

	if (port > 0) {
		if (udp_srv_init(&hkcp->udp_srv, port, (io_func_t) hkcp_udp_event, hkcp)) {
			goto DONE;
		}

		if (tcp_srv_init(&hkcp->tcp_srv, port, hkcp_tcp_event, hkcp)) {
			goto DONE;
		}
	}

	if (!opt_daemon) {
		command_t *cmd = command_new((command_handler_t) hkcp_command_stdin, hkcp);
		io_channel_setup(&hkcp->chan_stdin, fileno(stdin), (io_func_t) command_recv, cmd);
	}

	/* Feed list of explicit host addresses */
	hkcp_init_hosts(hkcp);

	/* Init network interface check */
	if (port > 0) {
		hkcp->ninterfaces = netif_show_interfaces();
		sys_timeout(INTERFACE_CHECK_DELAY, (sys_func_t) hkcp_check_interfaces, hkcp);
	}

	/* Init history logging */
	history_init();

	ret = 0;

DONE:
	if (ret < 0) {
		if (hkcp->udp_srv.chan.fd > 0) {
			udp_srv_shutdown(&hkcp->udp_srv);
		}

		if (hkcp->tcp_srv.csock.chan.fd > 0) {
			tcp_srv_shutdown(&hkcp->tcp_srv);
		}

		memset(hkcp, 0, sizeof(hkcp_t));
	}

	return ret;
}


void hkcp_monitor(hkcp_t *hkcp, hkcp_sink_func_t func, void *user_data)
{
	/* Raise monitor mode flag */
	hkcp->monitor.func = func;
	hkcp->monitor.user_data = user_data;

	/* Broadcast monitoring request */
	hkcp_advertise(hkcp);
}
