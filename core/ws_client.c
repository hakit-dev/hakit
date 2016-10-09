/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2015 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <libwebsockets.h>

#include "log.h"
#include "buf.h"
//#include "ws.h"
#include "ws_utils.h"
#include "ws_client.h"

#define MAX_BUF_LEN (256*1024)

struct per_session_data__client {
	int completed;
	buf_t buf;
	ws_client_func_t *func;
	void *user_data;
};


static void ws_http_client_read(struct lws *wsi, struct per_session_data__client *pss)
{
	char buffer[4096 + LWS_PRE];
	char *px = buffer + LWS_PRE;
	int lenx = sizeof(buffer) - LWS_PRE;
	int ret;

	/*
	 * Often you need to flow control this by something
	 * else being writable.  In that case call the api
	 * to get a callback when writable here, and do the
	 * pending client read in the writeable callback of
	 * the output.
	 */
	ret = lws_http_client_read(wsi, &px, &lenx);
	if (ret < 0) {
		log_debug(2, "ws_http_client_read => CLOSED", ret);
		//TODO: Free the pss that was allocated when initiating client connection
	}
	else {
		log_debug(3, "ws_http_client_read: partial len=%d", lenx);
		//log_debug_data((unsigned char *) px, lenx);

		if ((pss->buf.len+lenx) < MAX_BUF_LEN) {
			buf_append(&pss->buf, (unsigned char *) px, lenx);
		}

		if (pss->completed) {
			log_debug(2, "ws_http_client_read: completed len=%d", pss->buf.len);
			log_debug_data(pss->buf.base, pss->buf.len);

			// Callback received data
			if (pss->func != NULL) {
				pss->func(pss->user_data, pss->buf.base, pss->buf.len);
			}

			buf_cleanup(&pss->buf);
		}
	}
}


static int ws_client_callback(struct lws *wsi,
			      enum lws_callback_reasons reason, void *user,
			      void *in, size_t len)
{
	struct lws_context *context = lws_get_context(wsi);
	struct per_session_data__client *pss = (struct per_session_data__client *) user;
	struct lws_pollargs *pa = (struct lws_pollargs *) in;
	int ret = 0;

	log_debug(3, "ws_client_callback reason=%d pss=%p", reason, pss);

	switch (reason) {
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CLIENT_CONNECTION_ERROR: %s", (char *) in);
		break;
	case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH");
		break;
	case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER");
		break;
	case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
		log_debug(3, "ws_client_callback LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP");
		pss->completed = 0;
		buf_init(&pss->buf);
		break;
	case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CLOSED_CLIENT_HTTP");
		break;
	case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
		log_debug(3, "ws_client_callback LWS_CALLBACK_RECEIVE_CLIENT_HTTP");
		ws_http_client_read(wsi, pss);
		break;
	case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
		log_debug(3, "ws_client_callback LWS_CALLBACK_COMPLETED_CLIENT_HTTP");
		pss->completed = 1;
		break;
	case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
		log_debug(3, "ws_client_callback LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ");
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
		//FIXME: client wsi never destroyed after connection close ???
		break;

	case LWS_CALLBACK_LOCK_POLL:
		//log_debug(3, "ws_http_callback LWS_CALLBACK_LOCK_POLL");
		/*
		 * lock mutex to protect pollfd state
		 * called before any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_UNLOCK_POLL:
		//log_debug(3, "ws_http_callback LWS_CALLBACK_UNLOCK_POLL");
		/*
		 * unlock mutex to protect pollfd state when
		 * called after any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_ADD_POLL_FD:
		log_debug(3, "ws_client_callback LWS_CALLBACK_ADD_POLL_FD %d %02X", pa->fd, pa->events);
		ws_poll(context, pa);
		break;
	case LWS_CALLBACK_DEL_POLL_FD:
		log_debug(3, "ws_client_callback LWS_CALLBACK_DEL_POLL_FD %d", pa->fd);
		ws_poll_remove(pa);
		break;
	case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
		log_debug(3, "ws_client_callback LWS_CALLBACK_CHANGE_MODE_POLL_FD %d %02X", pa->fd, pa->events);
		ws_poll(context, pa);
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


int ws_client_get(ws_client_t *client, char *uri, ws_client_func_t *func, void *user_data)
{
	struct lws_client_connect_info i;
	struct per_session_data__client *pss;
	const char *prot, *p;
	char path[1024];
	int use_ssl = 0;
	struct lws *wsi;
	int ret;

	log_debug(2, "ws_client '%s'", uri);

	memset(&i, 0, sizeof(i));
	i.port = 80;

	ret = lws_parse_uri(uri, &prot, &i.address, &i.port, &p);
	if (ret) {
		return -1;
	}

#ifdef WITH_SSL
	if (strcmp(prot, "https") == 0) {
		use_ssl = client->use_ssl;
	}
#endif

	pss = malloc(sizeof(struct per_session_data__client));
	memset(pss, 0, sizeof(struct per_session_data__client));
	pss->func = func;
	pss->user_data = user_data;

	snprintf(path, sizeof(path), "/%s", p);
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

	log_debug(2, "ws_client addr='%s' port=%d ssl=%d", i.address, i.port, use_ssl);

	wsi = lws_client_connect_via_info(&i);
	log_debug(2, "ws_client => wsi=%p", wsi);

	return 0;
}
