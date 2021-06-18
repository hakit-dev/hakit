/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2015 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <libwebsockets.h>

#include "lws_config.h"
#include "options.h"
#include "log.h"
#include "buf.h"
#include "netif.h"
#include "ws_log.h"
#include "ws_io.h"
#include "ws_client.h"


#define MAX_BUF_LEN (256*1024)

struct per_session_data__client {
        ws_client_t *client;
	buf_t headers;
	buf_t buf;
	ws_client_func_t *func;
	void *user_data;
};


static int ws_http_client_read(struct lws *wsi, struct per_session_data__client *pss)
{
	char buffer[4096 + LWS_PRE];
	char *px = buffer + LWS_PRE;
	int lenx = sizeof(buffer) - LWS_PRE;
	int ret;

	ret = lws_http_client_read(wsi, &px, &lenx);
        log_debug(3, "ws_http_client_read => %d, lenx=%d", ret, lenx);

	return ret;
}


static void ws_http_client_read_chunk(struct lws *wsi, struct per_session_data__client *pss,
                                     char *buf, int len)
{
        log_debug(3, "ws_http_client_read_chunk: len=%d", len);

        if (len > 0) {
                if ((pss->buf.len+len) < MAX_BUF_LEN) {
                        buf_append(&pss->buf, (unsigned char *) buf, len);
                }
        }
}


static void ws_http_client_read_completed(struct lws *wsi, struct per_session_data__client *pss)
{
        log_debug(3, "ws_http_client_read_completed: len=%d", pss->buf.len);
        log_debug_data(pss->buf.base, pss->buf.len);

        // Callback received data
        if (pss->func != NULL) {
                pss->func(pss->user_data, (char *) pss->buf.base, pss->buf.len);
        }
 
        buf_cleanup(&pss->buf);
}


static int ws_http_client_append_header(struct lws *wsi, struct per_session_data__client *pss,
					char **hbuf, int hlen)
{
	int ofs = 0;

	// Network addresses
        if (pss->client->socket_signature == NULL) {
                pss->client->socket_signature = netif_socket_signature(lws_get_socket_fd(wsi));
        }

        if (pss->client->socket_signature != NULL) {
                int len = snprintf(*hbuf, hlen, "HAKit-Device: %s\r\n", pss->client->socket_signature);
                ofs += len;
                hlen -= len;
        }

	// User specified header
	if (pss->headers.len > 0) {
		if (hlen > pss->headers.len) {
			memcpy(*hbuf+ofs, pss->headers.base, pss->headers.len);
			ofs += pss->headers.len;
			hlen -= pss->headers.len;
		}
		else {
			log_str("ERROR: HTTP client header (user) too long (%d/%d)", pss->headers.len, hlen);
			return -1;
		}
	}

	if (ofs > 0) {
		log_debug_data((unsigned char *) *hbuf, ofs);
		*hbuf += ofs;
	}

	return ofs;
}


static int ws_client_callback(struct lws *wsi,
			      enum lws_callback_reasons reason, void *user,
			      void *in, size_t len)
{
	struct lws_context *context = lws_get_context(wsi);
	struct per_session_data__client *pss = (struct per_session_data__client *) user;
	int ret = 0;

	//log_debug(3, "ws_client_callback wsi=%p reason=%d pss=%p", wsi, reason, pss);

	switch (reason) {
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CLIENT_CONNECTION_ERROR: %s", (char *) in);
		// Callback error
		if (pss->func != NULL) {
			pss->func(pss->user_data, NULL, -1);
		}
		ret = 1;
		break;
	case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH %d", len);
                // Check for header
		break;
	case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: len=%d", len);

		if (ws_http_client_append_header(wsi, pss, in, len) < 0) {
			ret = 1;
		}
		break;
	case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
		log_debug(3, "ws_client_callback LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP");
		break;
	case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CLOSED_CLIENT_HTTP");
		break;
	case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
		log_debug(3, "ws_client_callback LWS_CALLBACK_RECEIVE_CLIENT_HTTP");
		ret = ws_http_client_read(wsi, pss);
		break;
	case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
		log_debug(3, "ws_client_callback LWS_CALLBACK_COMPLETED_CLIENT_HTTP");
                ws_http_client_read_completed(wsi, pss);
		break;
	case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
		log_debug(3, "ws_client_callback LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: len=%d", len);
                ws_http_client_read_chunk(wsi, pss, in, len);
		break;

	case LWS_CALLBACK_PROTOCOL_INIT:
		log_debug(3, "ws_client_callback LWS_CALLBACK_PROTOCOL_INIT");
		break;
	case LWS_CALLBACK_PROTOCOL_DESTROY:
		log_debug(3, "ws_client_callback LWS_CALLBACK_PROTOCOL_DESTROY");
		break;
	case LWS_CALLBACK_WSI_CREATE:
		log_debug(3, "ws_client_callback LWS_CALLBACK_WSI_CREATE");
		break;
	case LWS_CALLBACK_WSI_DESTROY:
		log_debug(3, "ws_htclienttp_callback LWS_CALLBACK_WSI_DESTROY");
		// Free the pss that was allocated when initiating client connection
		buf_cleanup(&pss->headers);
		buf_cleanup(&pss->buf);
		memset(pss, 0, sizeof(struct per_session_data__client));
		free(pss);
		break;

	case LWS_CALLBACK_LOCK_POLL:
		log_debug(3, "ws_http_callback LWS_CALLBACK_LOCK_POLL");
		/*
		 * lock mutex to protect pollfd state
		 * called before any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_UNLOCK_POLL:
		log_debug(3, "ws_http_callback LWS_CALLBACK_UNLOCK_POLL");
		/*
		 * unlock mutex to protect pollfd state when
		 * called after any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_ADD_POLL_FD:
	        {
			struct lws_pollargs *pa = (struct lws_pollargs *) in;
			log_debug(3, "ws_client_callback LWS_CALLBACK_ADD_POLL_FD %d %02X", pa->fd, pa->events);
			ws_poll(context, pa);
		}
		break;
	case LWS_CALLBACK_DEL_POLL_FD:
	        {
			struct lws_pollargs *pa = (struct lws_pollargs *) in;
			log_debug(3, "ws_client_callback LWS_CALLBACK_DEL_POLL_FD %d", pa->fd);
			ws_poll_remove(pa);
		}
		break;
	case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
	        {
			struct lws_pollargs *pa = (struct lws_pollargs *) in;
			log_debug(3, "ws_client_callback LWS_CALLBACK_CHANGE_MODE_POLL_FD %d %02X", pa->fd, pa->events);
			ws_poll(context, pa);
		}
		break;

	default:
		log_debug(3, "ws_client_callback reason=%d", reason);
		break;
	}

	return ret;
}


static struct lws_protocols ws_client_protocols[] = {
	/* first protocol must always be HTTP handler */
	{
		.name = "http-client",
		.callback = ws_client_callback,
		.per_session_data_size = sizeof(struct per_session_data__client),
		.rx_buffer_size = 0,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};


int ws_client_init(ws_client_t *client, int use_ssl)
{
	struct lws_context_creation_info info;

	log_str("Using libwebsockets version " LWS_LIBRARY_VERSION " build " LWS_BUILD_HASH);

	// Setup LWS logging
	ws_log_init(opt_debug);

	// Init HTTP client context
	memset(&info, 0, sizeof(info));

	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = ws_client_protocols;
	info.gid = -1;
	info.uid = -1;
	info.user = client;

#ifdef WITH_SSL
	/* Setup client SSL info */
	info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	info.ssl_ca_filepath = "/etc/ssl/certs/ca-certificates.crt";
#endif

	client->context = lws_create_context(&info);
	if (client->context == NULL) {
		return -1;
	}

	client->use_ssl = use_ssl;

	return 0;
}


void ws_client_destroy(ws_client_t *client)
{
	/* Destroy libwebsockets contexts */
	if (client->context != NULL) {
		lws_context_destroy((struct lws_context *) client->context);
		client->context = NULL;
	}

        if (client->socket_signature != NULL) {
                free(client->socket_signature);
                client->socket_signature = NULL;
        }
}


static const struct lws_extension exts[] = {
	{
		"permessage-deflate",
		lws_extension_callback_pm_deflate,
		"permessage-deflate; client_max_window_bits"
	},
	{
		"deflate-frame",
		lws_extension_callback_pm_deflate,
		"deflate_frame"
	},
	{ NULL, NULL, NULL /* terminator */ }
};


int ws_client_get(ws_client_t *client, char *uri, char *headers, ws_client_func_t *func, void *user_data)
{
	struct lws_client_connect_info i;
	struct per_session_data__client *pss;
	char buf[1024];
	const char *prot = NULL;
	const char *p = NULL;
	char *path;
	int use_ssl = 0;
	struct lws *wsi;
	int ret;

	log_debug(2, "ws_client_get '%s'", uri);

	memset(&i, 0, sizeof(i));
	i.port = 80;

	strncpy(buf, uri, sizeof(buf)-1);
	buf[sizeof(buf)-1] = '\0';
	ret = lws_parse_uri(buf, &prot, &i.address, &i.port, &p);
	if (ret) {
		return -1;
	}

	if (p != NULL) {
		if (*p == '/') {
			path = strdup(p);
		}
		else {
			int size = strlen(p) + 2;
			path = malloc(size);
			snprintf(path, size, "/%s", p);
		}
	}
	else {
		path = strdup("/");
	}

#ifdef WITH_SSL
	if (strcmp(prot, "https") == 0) {
		use_ssl = client->use_ssl;
	}
#endif

	pss = malloc(sizeof(struct per_session_data__client));
	memset(pss, 0, sizeof(struct per_session_data__client));

        pss->client = client;

	buf_init(&pss->headers);
	if (headers != NULL) {
		buf_append_str(&pss->headers, headers);
	}

	buf_init(&pss->buf);
	pss->func = func;
	pss->user_data = user_data;

	i.context = client->context;
	i.ssl_connection = use_ssl;
	i.path = path;
	i.host = i.address;
	i.origin = i.address;
	i.protocol = ws_client_protocols[0].name;
	i.ietf_version_or_minus_one = -1;
	i.userdata = pss;
	i.client_exts = exts;
	i.method = "GET";

	log_debug(2, "ws_client_get ssl=%d port=%d addr='%s' path='%s'", use_ssl, i.port, i.address, i.path);

	wsi = lws_client_connect_via_info(&i);
	log_debug(3, "ws_client_get => wsi=%p", wsi);

	if (path != NULL) {
		free(path);
		path = NULL;
	}

	return (wsi != NULL) ? 0:-1;
}
