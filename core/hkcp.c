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
#include "hkcp.h"
#include "hkcp_cmd.h"


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

static void hkcp_node_send_initial_value(hkcp_node_t *node, hk_source_t *source);


/*
 * Nodes
 */

static int hkcp_node_source_attached(hkcp_node_t *node, hk_source_t *source)
{
	int count = 0;
	int i;

	for (i = 0; i < node->sources.nmemb; i++) {
		hk_source_t *source2 = HK_TAB_VALUE(node->sources, hk_source_t *, i);
		if ((source2 != NULL) && ((source == NULL) || (source2 == source))) {
			count++;
		}
	}

	return count;
}


static void hkcp_node_source_attach(hkcp_node_t *node, hk_source_t *source)
{
	if (hkcp_node_source_attached(node, source)) {
		return;
	}

	hk_source_t **psource = hk_tab_push(&node->sources);
	*psource = source;

	log_debug(2, "hkcp_node_source_attach node=#%d='%s' source='%s.%s' (%d elements)", node->id, node->name, source->ep.obj->tile->name, source->ep.obj->name, node->sources.nmemb);
}


#if 0
static void hkcp_node_source_detach(hkcp_node_t *node, hk_source_t *source)
{
	int i;

	if (source == NULL) {
		return;
	}

	log_debug(2, "hkcp_node_source_detach node=#%d='%s' source='%s'", node->id, node->name, source->ep.name);

	for (i = 0; i < node->sources.nmemb; i++) {
		hk_source_t **psource = HK_TAB_PTR(node->sources, hk_source_t *, i);
		if ((*psource != NULL) && ((source == NULL) ||(*psource == source))) {
			*psource = NULL;
		}
	}
}
#endif


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
	hk_tab_init(&node->sources, sizeof(hk_source_t *));

	return node;
}


static void hkcp_node_set_state(hkcp_node_t *node, hkcp_node_state_t state)
{
	log_debug(2, "hkcp_node_set_state node=#%d='%s' %d -> %d", node->id, node->name, node->state, state);
	node->state = state;
	node->rbuf.len = 0;
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
		if (hkcp_node_source_attached(node, NULL) > 0) {
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
		hk_source_t *source = hk_source_retrieve_by_name(node->hkcp->eps, str);

		/* If matching source is found, check for requesting node connection */
		if (source != NULL) {
			if (hk_source_is_public(source)) {
				log_debug(2, "  remote sink='%s', matching source='%s.%s'", str, source->ep.obj->tile->name, source->ep.obj->name);

				/* Attach source to node (if not already done) */
				hkcp_node_source_attach(node, source);

				/* Send initial value */
				hkcp_node_send_initial_value(node, source);
			}
			else {
				log_debug(2, "  remote sink='%s', matching source='%s.%s' (local)", str, source->ep.obj->tile->name, source->ep.obj->name);
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

	/* Clear table of sources */
	hk_tab_cleanup(&node->sources);

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
	if (tcp_sock_connect(&node->tcp_sock, node->name, node->hkcp->port, node->hkcp->certs, hkcp_node_event, node) > 0) {
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


void hkcp_node_add(hkcp_t *hkcp, char *remote_ip)
{
	log_debug(2, "hkcp_node_add %s", remote_ip);

	/* Do nothing if HKCP is disabled */
	if (hkcp->port <= 0) {
		return;
	}

	/* Do nothing if we do not have any public source */
	if (hk_source_to_advertise(hkcp->eps) <= 0) {
		log_debug(2, "  => No public source found");
		return;
	}

	/* Try to recycle unused entry */
	hkcp_node_t *node = hkcp_node_retrieve(hkcp, remote_ip);

	/* Allocate new node if no recycled entry found */
	if (node == NULL) {
		node = hkcp_node_alloc(hkcp);
		node->name = strdup(remote_ip);
	}

	/* Try to connect */
	if (!tcp_sock_is_connected(&node->tcp_sock)) {
		if (node->timeout_tag == 0) {
			node->timeout_tag = sys_timeout(10, (sys_func_t) hkcp_node_connect_first, node);
		}
	}
}


void hkcp_node_dump(hkcp_t *hkcp, hk_source_t *source, buf_t *out_buf)
{
        int i;

	/* Dump all nodes having this source */
        for (i = 0; i < hkcp->nodes.nmemb; i++) {
                hkcp_node_t *node = HK_TAB_VALUE(hkcp->nodes, hkcp_node_t *, i);

		if (node != NULL) {
			if ((source == NULL) || hkcp_node_source_attached(node, source)) {
				buf_append_str(out_buf, " ");
				buf_append_str(out_buf, node->name);
			}
		}
        }
}


static void hkcp_node_send_initial_value(hkcp_node_t *node, hk_source_t *source)
{
	log_debug(3, "hkcp_node_send_initial_value node=#%d='%s' source='%s.%s' flag=%02X", node->id, node->name, source->ep.obj->tile->name, source->ep.obj->name, source->ep.flag);

	/* Do not send initial value if source is declared as an event */
	if (hk_source_is_event(source)) {
		return;
	}

	/* Send initial value if node is attached to source */
	if (hkcp_node_source_attached(node, source)) {
		int size = strlen(source->ep.obj->name) + source->ep.value.len + 10;
		char str[size];
		int len;

		len = snprintf(str, size-1, "set %s=%s", source->ep.obj->name, source->ep.value.base);
		log_debug(2, "hkcp_node_send_initial_value node=#%d='%s' cmd='%s'", node->id, node->name, str);
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


static void hkcp_source_send_nodes(hkcp_t *hkcp, hk_source_t *source, char *str, int len)
{
	int i;

	for (i = 0; i < hkcp->nodes.nmemb; i++) {
		hkcp_node_t *node = HK_TAB_VALUE(hkcp->nodes, hkcp_node_t *, i);

		if (node != NULL) {
                        if (hkcp_node_source_attached(node, source)) {
                                log_debug(2, "    node=#%d='%s'", node->id, node->name);
                                tcp_sock_write(&node->tcp_sock, str, len);
                        }
                }
	}
}


void hkcp_source_update(hkcp_t *hkcp, hk_source_t *source, char *value)
{
	char *name = source->ep.obj->name;
	log_debug(3, "hkcp_source_update name='%s.%s' value='%s'", source->ep.obj->tile->name, name, value);

	int size = strlen(name) + strlen(value) + 20;
	char str[size];
	int len;

	/* Send update command to all nodes that subscribed this source */
	len = snprintf(str, size-1, "set %s=%s", name, value);
	log_debug(2, "  cmd='%s'", str);
	str[len++] = '\n';
	hkcp_source_send_nodes(hkcp, source, str, len);

	/* Send event to watchers */
	snprintf(str, size, "!%s=%s\n", name, value);
	tcp_srv_foreach_client(&hkcp->tcp_srv, (tcp_foreach_func_t) hkcp_source_send_watch, str);
}


/*
 * Per-connection command context
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


/*
 * TCP/stdin/websocket commands
 */

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
		hkcp_command_watch(hkcp->eps, argc, argv, &out_buf, &ctx->watch);
	}
	else {
		hkcp_command(hkcp, argc, argv, &out_buf);
	}

	tcp_sock_write(tcp_sock, (char *) out_buf.base, out_buf.len);

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

int hkcp_init(hkcp_t *hkcp, hk_endpoints_t *eps, int port, char *certs)
{
	int ret = -1;

	memset(hkcp, 0, sizeof(hkcp_t));

	hkcp->eps = eps;

	/* Init node management */
	hk_tab_init(&hkcp->nodes, sizeof(hkcp_node_t *));

	/* Init TCP server */
	hkcp->port = port;
        if (certs != NULL) {
                hkcp->certs = strdup(certs);
        }
	tcp_srv_clear(&hkcp->tcp_srv);

	if (port > 0) {
		if (tcp_srv_init(&hkcp->tcp_srv, port, certs, hkcp_tcp_event, hkcp)) {
			goto DONE;
		}
	}

	ret = 0;

DONE:
	if (ret < 0) {
		hkcp_shutdown(hkcp);
	}

	return ret;
}


void hkcp_shutdown(hkcp_t *hkcp)
{
	if (hkcp->tcp_srv.csock.chan.fd > 0) {
		tcp_srv_shutdown(&hkcp->tcp_srv);
	}

	hk_tab_cleanup(&hkcp->nodes);

        if (hkcp->certs != NULL) {
                free(hkcp->certs);
        }

	memset(hkcp, 0, sizeof(hkcp_t));
}
