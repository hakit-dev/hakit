/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include "types.h"
#include "log.h"
#include "options.h"
#include "buf.h"
#include "iputils.h"
#include "tcpio.h"
#include "udpio.h"
#include "command.h"
#include "comm.h"
#include "comm_priv.h"


#define HAKIT_COMM_PORT 5678   // Default HAKit communication port

#define ADVERTISE_DELAY 1000   // Delay before advertising a newly registered sink/source
#define ADVERTISE_MAXLEN 1200  // Maxim advertising packet length

#define UDP_SIGN        0xAC   // Advertising packet signature
#define UDP_TYPE_SINK   0x01   // Advertising field type: sink
#define UDP_TYPE_SOURCE 0x02   // Advertising field type: source
#define UDP_TYPE_MONITOR 0x03  // Advertising field type: monitor


/* Local functions forward declarations */
static void comm_udp_send(comm_t *comm, buf_t *buf, int reply);
static void comm_advertise(comm_t *comm, unsigned long delay);
static void comm_command(comm_t *comm, char *line, tcp_sock_t *tcp_sock);
static int comm_node_connect(comm_node_t *node);
static void comm_source_send_initial_value(comm_source_t *source, comm_node_t *node);



/*
 * Nodes
 */

static comm_node_t *comm_node_retrieve(comm_t *comm, char *name)
{
	int i;

	for (i = 0; i < comm->nodes.nmemb; i++) {
		comm_node_t *node = HK_TAB_VALUE(comm->nodes, comm_node_t *, i);
		if (node != NULL) {
			if (strcmp(node->name, name) == 0) {
				return node;
			}
		}
	}

	return NULL;
}


static void comm_node_command(char *line, comm_node_t *node)
{
	log_debug(2, "comm_node_command '%s'", line);

	if (line != NULL) {
		comm_command(node->comm, line, &node->tcp_sock);
	}
}


static comm_node_t *comm_node_alloc(comm_t *comm)
{
	comm_node_t *node;
	comm_node_t **pnode;
	int i;

	/* Alloc node descriptor */
	node = (comm_node_t *) malloc(sizeof(comm_node_t));
	memset(node, 0, sizeof(comm_node_t));

	/* Find entry in node table */
	for (i = 0; i < comm->nodes.nmemb; i++) {
		pnode = HK_TAB_PTR(comm->nodes, comm_node_t *, i);
		if (*pnode == NULL) {
			log_debug(2, "comm_node_alloc -> #%d (reused)", i);
			break;
		}
	}

	if (i >= comm->nodes.nmemb) {
		log_debug(2, "comm_node_alloc -> #%d (new)", i);
		pnode = hk_tab_push(&comm->nodes);
	}

	*pnode = node;

	/* Init node entry */
	node->id = i;
	tcp_sock_clear(&node->tcp_sock);
	node->comm = comm;
	node->cmd = command_new((command_handler_t) comm_node_command, node);

	return node;
}


static void comm_node_event(tcp_sock_t *tcp_sock, tcp_io_t io, char *rbuf, int rsize)
{
	comm_node_t *node = tcp_sock_get_data(tcp_sock);
	log_debug(2, "comm_node_event [%d] node=#%d='%s'", tcp_sock->chan.fd, node->id, node->name);
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
		comm_node_connect(node);
		break;
	default:
		log_debug(2, "  PANIC: unknown event caught");
		break;
	}
}


static void comm_node_detach_from_sources(comm_t *comm, comm_node_t *node)
{
	int i, j;

	log_debug(2, "comm_node_detach_from_sources node=#%d='%s'", node->id, node->name);

	for (i = 0; i < comm->sources.nmemb; i++) {
		comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, i);
		if (source->name != NULL) {
			for (j = 0; j < source->nodes.nmemb; j++) {
				comm_node_t **pnode = HK_TAB_PTR(source->nodes, comm_node_t *, j);
				if (*pnode == node) {
					log_debug(2, "  source='%s'", source->name);
					*pnode = NULL;
				}
			}
		}
	}
}


static void comm_node_remove(comm_node_t *node)
{
	log_debug(2, "comm_node_remove node=#%d='%s'", node->id, node->name);

	/* Free node entry for future use */
	comm_node_t **pnode = HK_TAB_PTR(node->comm->nodes, comm_node_t *, node->id);
	*pnode = NULL;

	/* Kill running timeout */
	if (node->timeout_tag) {
		sys_remove(node->timeout_tag);
		node->timeout_tag = 0;
	}

	/* Detach from all sources */
	comm_node_detach_from_sources(node->comm, node);

	/* Shut down connection */
	tcp_sock_set_data(&node->tcp_sock, NULL);
	tcp_sock_shutdown(&node->tcp_sock);

	/* Clear command buffering context */
	command_clear(node->cmd);

	/* Free node name */
	free(node->name);
	node->name = NULL;

	/* Free node descriptor */
	memset(node, 0, sizeof(comm_node_t));
	free(node);
}


static void comm_node_send_initial_values(comm_node_t *node)
{
	comm_t *comm = node->comm;
	int i;

	for (i = 0; i < comm->sources.nmemb; i++) {
		comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, i);
		comm_source_send_initial_value(source, node);
	}
}


static int comm_node_connect(comm_node_t *node)
{
	node->connect_attempts++;

	log_str("Connecting to node #%d='%s' (%d/4)", node->id, node->name, node->connect_attempts);
	if (tcp_sock_connect(&node->tcp_sock, node->name, node->comm->udp_srv.port, comm_node_event, node) > 0) {
		node->timeout_tag = 0;
		comm_node_send_initial_values(node);
		return 0;
	}

	if (node->connect_attempts > 4) {
		log_str("Too many connections attempted on node #%d='%s': giving up", node->id, node->name);
		node->timeout_tag = 0;
		comm_node_remove(node);
		return 0;
	}

	if (node->timeout_tag == 0) {
		node->timeout_tag = sys_timeout(5000, (sys_func_t) comm_node_connect, node);
	}

	return 1;
}


static int comm_node_connect_first(comm_node_t *node)
{
	node->timeout_tag = 0;
	node->connect_attempts = 0;
	comm_node_connect(node);
	return 0;
}


static comm_node_t *comm_node_add(comm_t *comm, char *name)
{
	log_debug(2, "comm_node_add node='%s'", name);

	comm_node_t *node = comm_node_retrieve(comm, name);

	/* Allocate entry */
	if (node == NULL) {
		node = comm_node_alloc(comm);
		node->name = strdup(name);
	}

	if (!tcp_sock_is_connected(&node->tcp_sock)) {
		if (node->timeout_tag == 0) {
			node->timeout_tag = sys_timeout(10, (sys_func_t) comm_node_connect_first, node);
		}
	}

	return node;
}


/*
 * Sinks
 */

static void comm_sink_advertise(comm_t *comm, int reply)
{
	int nsinks = comm->sinks.nmemb;
	buf_t buf;
	int i;

	if (nsinks <= 0) {
		return;
	}

	log_str("Advertising %d sink%s as %s", nsinks, (nsinks > 1) ? "s":"", reply ? "reply":"broadcast");

	buf_init(&buf);

	for (i = 0; i < nsinks; i++) {
		comm_sink_t *sink = HK_TAB_PTR(comm->sinks, comm_sink_t, i);
		if (sink->name != NULL) {
			if (buf.len == 0) {
				buf_append_byte(&buf, UDP_SIGN);
				buf_append_byte(&buf, UDP_TYPE_SINK);
			}
			buf_append_str(&buf, sink->name);
			buf_append_byte(&buf, 0);

			if (buf.len > ADVERTISE_MAXLEN) {
				comm_udp_send(comm, &buf, reply);
			}
		}
	}

	if (buf.len > 0) {
		comm_udp_send(comm, &buf, reply);
	}

	buf_cleanup(&buf);
}


static comm_sink_t *comm_sink_retrieve(comm_t *comm, char *name)
{
	int i;

	for (i = 0; i < comm->sinks.nmemb; i++) {
		comm_sink_t *sink = HK_TAB_PTR(comm->sinks, comm_sink_t, i);
		if (sink->name != NULL) {
			if (strcmp(sink->name, name) == 0) {
				return sink;
			}
		}
	}

	return NULL;
}


static comm_sink_t *comm_sink_alloc(comm_t *comm)
{
	comm_sink_t *sink = NULL;
	int i;

	for (i = 0; i < comm->sinks.nmemb; i++) {
		sink = HK_TAB_PTR(comm->sinks, comm_sink_t, i);
		if (sink->name == NULL) {
			return sink;
		}
	}

	sink = hk_tab_push(&comm->sinks);

	buf_init(&sink->value);

	return sink;
}


static void comm_sink_create(comm_t *comm, char *name, comm_sink_func_t func, void *user_data)
{
	comm_sink_t *sink = comm_sink_retrieve(comm, name);

	if (sink == NULL) {
		sink = comm_sink_alloc(comm);
		sink->name = strdup(name);
	}

	buf_set_str(&sink->value, "");
	sink->func = func;
	sink->user_data = user_data;
}


static void comm_sink_register_(comm_t *comm, char *name, comm_sink_func_t func, void *user_data)
{
	comm_sink_create(comm, name, func, user_data);
	log_debug(2, "comm_sink_register sink='%s' (%d elements)", name, comm->sinks.nmemb);
	comm_advertise(comm, ADVERTISE_DELAY);
}


#if 0
static void comm_sink_unregister_(comm_t *comm, char *name)
{
	comm_sink_t *sink = comm_sink_retrieve(comm, name);

	if (sink != NULL) {
		buf_cleanup(&sink->value);
		free(sink->name);
		memset(sink, 0, sizeof(comm_sink_t));
	}
}
#endif


/*
 * Sources
 */

static void comm_source_advertise(comm_t *comm, int reply)
{
	int nsources = comm->sources.nmemb;
	buf_t buf;
	int i;

	if (nsources <= 0) {
		return;
	}

	log_str("Advertising %d source%s as %s", nsources, (nsources > 1) ? "s":"", reply ? "reply":"broadcast");

	buf_init(&buf);

	for (i = 0; i < nsources; i++) {
		comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, i);
		if (source->name != NULL) {
			if (buf.len == 0) {
				buf_append_byte(&buf, UDP_SIGN);
				buf_append_byte(&buf, UDP_TYPE_SOURCE);
			}
			buf_append_str(&buf, source->name);
			buf_append_byte(&buf, 0);

			if (buf.len > ADVERTISE_MAXLEN) {
				comm_udp_send(comm, &buf, reply);
			}
		}
	}

	if (buf.len > 0) {
		comm_udp_send(comm, &buf, reply);
	}

	buf_cleanup(&buf);
}


static comm_source_t *comm_source_retrieve(comm_t *comm, char *name)
{
	int i;

	for (i = 0; i < comm->sources.nmemb; i++) {
		comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, i);
		if (source->name != NULL) {
			if (strcmp(source->name, name) == 0) {
				return source;
			}
		}
	}

	return NULL;
}


static comm_source_t *comm_source_alloc(comm_t *comm)
{
	comm_source_t *source = NULL;
	int i;

	for (i = 0; i < comm->sources.nmemb; i++) {
		source = HK_TAB_PTR(comm->sources, comm_source_t, i);
		if (source->name == NULL) {
			source->id = i;
			return source;
		}
	}

	i = comm->sources.nmemb;
	source = hk_tab_push(&comm->sources);

	source->id = i;
	buf_init(&source->value);

	hk_tab_init(&source->nodes, sizeof(comm_node_t *));

	return source;
}


static int comm_source_register_(comm_t *comm, char *name, int event)
{
	comm_source_t *source = comm_source_retrieve(comm, name);

	if (source == NULL) {
		source = comm_source_alloc(comm);
		source->name = strdup(name);
	}

	log_debug(2, "comm_source_register name='%s' (%d elements)", name, comm->sources.nmemb);

	buf_set_str(&source->value, "");
	source->event = event;

	comm_advertise(comm, ADVERTISE_DELAY);

	return source->id;
}


#if 0
static void comm_source_unregister_(comm_t *comm, char *name)
{
	comm_source_t *source = comm_source_retrieve(comm, name);

	log_debug(2, "comm_source_unregister name='%s'", name);

	if (source != NULL) {
		free(source->name);
		buf_cleanup(&source->value);
		memset(source, 0, sizeof(comm_source_t));
	}
}
#endif


static int comm_source_node_attached(comm_source_t *source, comm_node_t *node)
{
	int i;

	if (source->name == NULL) {
		return 0;
	}

	for (i = 0; i < source->nodes.nmemb; i++) {
		comm_node_t *node2 = HK_TAB_VALUE(source->nodes, comm_node_t *, i);
		if (node2 == node) {
			return 1;
		}
	}

	return 0;
}


static void comm_source_attach_node(comm_source_t *source, comm_node_t *node)
{
	int i;

	if (comm_source_node_attached(source, node)) {
		return;
	}

	for (i = 0; i < source->nodes.nmemb; i++) {
		comm_node_t **pnode = HK_TAB_PTR(source->nodes, comm_node_t *, i);
		if (*pnode == NULL) {
			*pnode = node;
			return;
		}
	}

	comm_node_t **pnode = hk_tab_push(&source->nodes);
	*pnode = node;

	log_debug(2, "comm_source_attach_node source='%s' node=#%d='%s' (%d elements)", source->name, node->id, node->name, source->nodes.nmemb);
}


static void comm_source_send_initial_value(comm_source_t *source, comm_node_t *node)
{
	log_debug(3, "comm_source_send_initial_value source='%s' event=%d node=#%d='%s'", source->name, source->event, node->id, node->name);

	/* Do not send initial value if source is declared as an event */
	if (source->event) {
		return;
	}

	/* Send initial value if node is attached to source */
	if (comm_source_node_attached(source, node)) {
		int size = strlen(source->name) + source->value.len + 10;
		char str[size];
		int len;

		len = snprintf(str, size-1, "set %s=%s", source->name, source->value.base);
		log_debug(2, "comm_source_send_initial_value cmd='%s' node=#%d='%s'", str, node->id, node->name);
		str[len++] = '\n';

		tcp_sock_write(&node->tcp_sock, str, len);
	}
}


static void comm_source_send(comm_t *comm, int id)
{
	comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, id);

	if (source->name != NULL) {
		int size = strlen(source->name) + source->value.len + 10;
		char str[size];
		int len;
		int i;

		len = snprintf(str, size-1, "set %s=%s", source->name, source->value.base);
		log_debug(2, "comm_source_send cmd='%s' (%d nodes)", str, source->nodes.nmemb);
		str[len++] = '\n';

		for (i = 0; i < source->nodes.nmemb; i++) {
			comm_node_t *node = HK_TAB_VALUE(source->nodes, comm_node_t *, i);
			if (node != NULL) {
				log_debug(2, "  node=#%d='%s'", node->id, node->name);
				tcp_sock_write(&node->tcp_sock, str, len);
			}
		}
	}
	else {
		log_str("PANIC: Attempting to send data to unknown source #%d\n", id);
	}
}


static void comm_source_update_str_(comm_t *comm, int id, char *value)
{
	comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, id);

	if (source->name != NULL) {
		buf_set_str(&source->value, value);
		comm_source_send(comm, id);
	}
	else {
		log_str("PANIC: Attempting to update unknown source #%d\n", id);
	}
}


static void comm_source_update_int_(comm_t *comm, int id, int value)
{
	comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, id);

	if (source->name != NULL) {
		buf_set_int(&source->value, value);
		comm_source_send(comm, id);
	}
	else {
		log_str("PANIC: Attempting to update unknown source #%d\n", id);
	}
}


/*
 * UDP Commands
 */

static void comm_udp_send(comm_t *comm, buf_t *buf, int reply)
{
	if (reply) {
		udp_srv_send_reply(&comm->udp_srv, (char *) buf->base, buf->len);
	}
	else {
		udp_srv_send_bcast(&comm->udp_srv, (char *) buf->base, buf->len);
	}

	buf->len = 0;
}


static void comm_udp_event_sink(comm_t *comm, int argc, char **argv)
{
	int i;

	log_debug(2, "comm_udp_event_sink (%d sinks)", argc);

	/* Check for local sources matching advertised sinks */
	for (i = 0; i < argc; i++) {
		char *args = argv[i];
		comm_source_t *source = comm_source_retrieve(comm, args);

		/* If matching source is found, check for requesting node connection */
		if (source != NULL) {
			log_debug(2, "  sink='%s', source='%s'", args, source->name);

			struct sockaddr_in *addr = udp_srv_remote(&comm->udp_srv);
			unsigned long addr_v = ntohl(addr->sin_addr.s_addr);
			char name[32];
			comm_node_t *node;

			/* Get remote IP address as node name */
			snprintf(name, sizeof(name), "%lu.%lu.%lu.%lu",
				 (addr_v >> 24) & 0xFF, (addr_v >> 16) & 0xFF, (addr_v >> 8) & 0xFF, addr_v & 0xFF);

			/* Connect to node (if not already done) */
			node = comm_node_add(comm, name);

			/* Attach source to node (if not already done) */
			comm_source_attach_node(source, node);
		}
		else {
			log_debug(2, "  sink='%s', no source", args);
		}
	}
}


static void comm_udp_event_source(comm_t *comm, int argc, char **argv)
{
	int found = 0;
	int i;

	log_debug(2, "comm_udp_event_source (%d sources)", argc);

	/* Check for local sinks matching advertised sources */
	for (i = 0; i < argc; i++) {
		char *name = argv[i];
		comm_sink_t *sink = comm_sink_retrieve(comm, name);

		/* Check for sinks matching sources in request list */
		if (sink != NULL) {
			found = 1;
			break;
		}
	}

	/* If matching source found, send back sink advertisement
	   as a reply to the requesting node */
	if (found) {
		log_debug(2, "  Matching sink found");
		comm_sink_advertise(comm, 1);
	}
	else {
		log_debug(2, "  No matching sink found");
	}
}


static void comm_udp_event(comm_t *comm, unsigned char *buf, int size)
{
	int argc = 0;
	char **argv = NULL;
	int i;
	int nl;

	log_debug(2, "comm_udp_event: %d bytes", size);
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
		comm_udp_event_sink(comm, argc, argv);
		break;
	case UDP_TYPE_SOURCE:
		/* If monitor mode enabled, tell sender we need to receive all sources events from it */
		if (comm->monitor_func != NULL) {
			for (i = 0; i < argc; i++) {
				comm->monitor_func(comm, argv[i], NULL);
			}

			buf[1] = UDP_TYPE_SINK;
			udp_srv_send_reply(&comm->udp_srv, (char *) buf, size);
		}
		else {
			comm_udp_event_source(comm, argc, argv);
		}
		break;
	case UDP_TYPE_MONITOR:
		comm_sink_advertise(comm, 1);
		comm_source_advertise(comm, 1);
		break;
	default:
		log_str("WARNING: Received UDP packet with unknown type (%02X)", buf[1]);
		break;
	}

	free(argv);
	argv = NULL;
}


/*
 * TCP/stdin commands
 */

typedef struct {
	comm_t *comm;
	tcp_sock_t *tcp_sock;
	command_t *cmd;
} comm_cmd_ctx_t;


static void comm_command_ctx(char *line, comm_cmd_ctx_t *ctx);


static comm_cmd_ctx_t *comm_cmd_ctx_new(tcp_sock_t *tcp_sock)
{
	comm_cmd_ctx_t *ctx;

	ctx = (comm_cmd_ctx_t *) malloc(sizeof(comm_cmd_ctx_t));
	ctx->comm = tcp_sock_get_data(tcp_sock);
	ctx->tcp_sock = tcp_sock;
	ctx->cmd = command_new((command_handler_t) comm_command_ctx, ctx);

	return ctx;
}


static void comm_cmd_ctx_destroy(comm_cmd_ctx_t *ctx)
{
	command_destroy(ctx->cmd);
	memset(ctx, 0, sizeof(comm_cmd_ctx_t));
	free(ctx);
}


static int comm_command_output(tcp_sock_t *tcp_sock, buf_t *out_buf)
{
	if (tcp_sock != NULL) {
		io_channel_write(&tcp_sock->chan, (char *) out_buf->base, out_buf->len);
	}
	else {
		fwrite(out_buf->base, 1, out_buf->len, stdout);
	}

	return 1;
}


static void comm_command_status(comm_t *comm, buf_t *out_buf)
{
	int i;

	buf_append_fmt(out_buf, "Nodes: %d\n", comm->nodes.nmemb);

	for (i = 0; i < comm->nodes.nmemb; i++) {
		comm_node_t *node = HK_TAB_VALUE(comm->nodes, comm_node_t *, i);
		int j;

		if (node->name != NULL) {
			buf_append_str(out_buf, "  ");
			buf_append_str(out_buf, node->name);

			for (j = 0; j < comm->sources.nmemb; j++) {
				comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, j);
				int k;

				if (source->name != NULL) {
					for (k = 0; k < source->nodes.nmemb; k++) {
						comm_node_t *node2 = HK_TAB_VALUE(source->nodes, comm_node_t *, k);
						if (node2 == node) {
							buf_append_str(out_buf, " ");
							buf_append_str(out_buf, source->name);
						}
					}
				}
			}

			buf_append_str(out_buf, "\n");
		}
	}

	buf_append_fmt(out_buf, "Sources: %d\n", comm->sources.nmemb);
	for (i = 0; i < comm->sources.nmemb; i++) {
		comm_source_t *source = HK_TAB_PTR(comm->sources, comm_source_t, i);
		int j;

		if (source->name != NULL) {
			buf_append_str(out_buf, "  ");
			buf_append_str(out_buf, source->name);

			for (j = 0; j < source->nodes.nmemb; j++) {
				comm_node_t *node = HK_TAB_VALUE(source->nodes, comm_node_t *, j);
				if (node != NULL) {
					buf_append_str(out_buf, " ");
					buf_append_str(out_buf, node->name);
				}
			}

			buf_append_str(out_buf, "\n");
		}
	}

	buf_append_fmt(out_buf, "Sinks: %d\n", comm->sinks.nmemb);
	for (i = 0; i < comm->sinks.nmemb; i++) {
		comm_sink_t *sink = HK_TAB_PTR(comm->sinks, comm_sink_t, i);

		if (sink->name != NULL) {
			buf_append_str(out_buf, "  ");
			buf_append_str(out_buf, sink->name);

			buf_append_str(out_buf, "\n");
		}
	}
}


static void comm_command_process(comm_t *comm, int argc, char **argv, buf_t *out_buf)
{
	int i;

	if (strcmp(argv[0], "set") == 0) {
		for (i = 1; i < argc; i++) {
			char *args = argv[i];
			char *value = strchr(args, '=');
			if (value != NULL) {
				*(value++) = '\0';
				comm_sink_t *sink = comm_sink_retrieve(comm, args);
				if (sink != NULL) {
					if (sink->func != NULL) {
						sink->func(sink->user_data, args, value);
					}
				}
				else {
					/* Send back error message if not in monitor mode */
					if (comm->monitor_func == NULL) {
						buf_append_str(out_buf, "ERROR: Unknown sink: ");
						buf_append_str(out_buf, args);
						buf_append_str(out_buf, "\n");
					}
				}

				if (comm->monitor_func != NULL) {
					comm->monitor_func(comm->monitor_user_data, args, value);
				}
			}
			else {
				buf_append_str(out_buf, "ERROR: Syntax error in command: ");
				buf_append_str(out_buf, args);
				buf_append_str(out_buf, "\n");
			}
		}
	}
	else if (strcmp(argv[0], "status") == 0) {
		comm_command_status(comm, out_buf);
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


static void comm_command(comm_t *comm, char *line, tcp_sock_t *tcp_sock)
{
	char **argv = NULL;
	int argc = 0;

	log_debug(2, "comm_command '%s'", line);

	if (strncmp(line, "ERROR:", 6) == 0) {
		return;
	}

	argc = command_parse(line, &argv);
	if (opt_debug >= 2) {
		int i;
		log_printf("  =>");
		for (i = 0; i < argc; i++) {
			log_printf(" [%d]=\"%s\"", i, argv[i]);
		}
		log_printf("\n");
	}

	if (argc > 0) {
		buf_t out_buf;

		buf_init(&out_buf);
		comm_command_process(comm, argc, argv, &out_buf);
		comm_command_output(tcp_sock, &out_buf);
		buf_cleanup(&out_buf);
	}

	if (argv != NULL) {
		free(argv);
	}
}


static void comm_command_stdin(char *line, comm_t *comm)
{
	if (line != NULL) {
		comm_command(comm, line, NULL);
	}
	else {
		/* Quit if hangup from stdin */
		sys_quit();
	}
}


static void comm_command_ctx(char *line, comm_cmd_ctx_t *ctx)
{
	log_debug(2, "comm_command_ctx '%s'", line);

	if (line != NULL) {
		comm_command(ctx->comm, line, ctx->tcp_sock);
	}
}


static void comm_tcp_event(tcp_sock_t *tcp_sock, tcp_io_t io, char *rbuf, int rsize)
{
	comm_cmd_ctx_t *ctx;

	log_debug(2, "comm_tcp_event [%d]", tcp_sock->chan.fd);
	log_debug_data((unsigned char *) rbuf, rsize);

	switch (io) {
	case TCP_IO_CONNECT:
		log_debug(2, "  CONNECT %s", rbuf);
		ctx = comm_cmd_ctx_new(tcp_sock);
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
		comm_cmd_ctx_destroy(ctx);
		break;
	default:
		log_str("  PANIC: unknown event caught");
		break;
	}
}


/*
 * Management engine
 */

static int comm_advertise_now(comm_t *comm)
{
	comm->advertise_tag = 0;
	comm_sink_advertise(comm, 0);
	comm_source_advertise(comm, 0);
	return 0;
}


static void comm_advertise(comm_t *comm, unsigned long delay)
{
	if (comm->advertise_tag != 0) {
		sys_remove(comm->advertise_tag);
		comm->advertise_tag = 0;
	}

	/* If no delay is provided, choose a random one */
	if (delay == 0) {
		comm_sink_advertise(comm, 1);
		comm_source_advertise(comm, 1);
	}
	else {
		log_debug(2, "Will send sink/source advertisement in %lu ms", delay);
		comm->advertise_tag = sys_timeout(delay, (sys_func_t) comm_advertise_now, comm);
	}
}


static int comm_init_(comm_t *comm, int port)
{
	int ret = -1;

	memset(comm, 0, sizeof(comm_t));
	udp_srv_clear(&comm->udp_srv);
	tcp_srv_clear(&comm->tcp_srv);
	hk_tab_init(&comm->nodes, sizeof(comm_node_t *));
	hk_tab_init(&comm->sinks, sizeof(comm_sink_t));
	hk_tab_init(&comm->sources, sizeof(comm_source_t));

	if (udp_check_interfaces() <= 0) {
		goto DONE;
	}

	if (udp_srv_init(&comm->udp_srv, port, (io_func_t) comm_udp_event, comm)) {
		goto DONE;
	}

	if (tcp_srv_init(&comm->tcp_srv, port, comm_tcp_event, comm)) {
		goto DONE;
	}

	if (!opt_daemon) {
		command_t *cmd = command_new((command_handler_t) comm_command_stdin, comm);
		io_channel_setup(&comm->chan_stdin, STDIN_FILENO, (io_func_t) command_recv, cmd);
	}

	ret = 0;

DONE:
	if (ret < 0) {
		if (comm->udp_srv.chan.fd > 0) {
			udp_srv_shutdown(&comm->udp_srv);
		}

		if (comm->tcp_srv.csock.chan.fd > 0) {
			tcp_srv_shutdown(&comm->tcp_srv);
		}
	}

	return ret;
}


static void comm_monitor_(comm_t *comm, comm_sink_func_t func, void *user_data)
{
	buf_t buf;

	/* Raise monitor mode flag */
	comm->monitor_func = func;
	comm->monitor_user_data = user_data;

	/* Broadcast monitoring request */
	buf_init(&buf);
	buf_append_byte(&buf, UDP_SIGN);
	buf_append_byte(&buf, UDP_TYPE_MONITOR);
	comm_udp_send(comm, &buf, 0);
	buf_cleanup(&buf);
}


/*
 * Global context primitives
 */

static comm_t hk_comm;

int comm_init(void)
{
	return comm_init_(&hk_comm, HAKIT_COMM_PORT);
}


void comm_monitor(comm_sink_func_t func, void *user_data)
{
	comm_monitor_(&hk_comm, func, user_data);
}


void comm_sink_register(char *name, comm_sink_func_t func, void *user_data)
{
	comm_sink_register_(&hk_comm, name, func, user_data);
}


int comm_source_register(char *name, int event)
{
	return comm_source_register_(&hk_comm, name, event);
}


void comm_source_update_str(int id, char *value)
{
	comm_source_update_str_(&hk_comm, id, value);
}


void comm_source_update_int(int id, int value)
{
	comm_source_update_int_(&hk_comm, id, value);
}
