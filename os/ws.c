/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>

#ifdef ENABLE_LWS
#include <libwebsockets.h>
#endif

#include "types.h"
#include "log.h"
#include "sys.h"
#include "ws.h"


static struct libwebsocket_context *ws_context = NULL;


/*
 * Protocol handling: HTTP
 */

struct per_session_data__http {
	int fd;
};

static int ws_callback_http(struct libwebsocket_context *context,
			    struct libwebsocket *wsi,
			    enum libwebsocket_callback_reasons reason, void *user,
			    void *in, size_t len)
{
	return 0;
}


/*
 * Protocol handling: HAKit events
 */

struct per_session_data__events {
	int foo;
};


static int ws_callback_events(struct libwebsocket_context *context,
			    struct libwebsocket *wsi,
			    enum libwebsocket_callback_reasons reason, void *user,
			    void *in, size_t len)
{
	return 0;
}



/*
 * Table of available protocols
 */

static struct libwebsocket_protocols ws_protocols[] = {
	/* first protocol must always be HTTP handler */

	{
		.name = "http-only",
		.callback = ws_callback_http,
		.per_session_data_size = sizeof(struct per_session_data__http),
		.rx_buffer_size = 0,
	},
	{
		.name = "hakit-events",
		.callback = ws_callback_events,
		.per_session_data_size = sizeof(struct per_session_data__events),
		.rx_buffer_size = 10,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};


int ws_init(int port, int use_ssl)
{
	struct lws_context_creation_info info;

	lwsl_notice("HAKit " xstr(HAKIT_VERSION));

	memset(&info, 0, sizeof info);
	info.port = port;
	info.protocols = ws_protocols;
	//info.extensions = libwebsocket_get_internal_extensions();

	/* TODO: etup SSL info */
	if (use_ssl) {
//		info.ssl_cert_filepath = cert_path;
//		info.ssl_private_key_filepath = key_path;
	}

	info.gid = -1;
	info.uid = -1;

	/* Create libwebsockets context */
	ws_context = libwebsocket_create_context(&info);
	if (ws_context == NULL) {
		log_str("ERROR: libwebsocket init failed");
		return -1;
	}

	return 0;
}


void ws_done(void)
{
	/* Destroy libwebsockets context */
	if (ws_context != NULL) {
		libwebsocket_context_destroy(ws_context);
		ws_context = NULL;
	}
}
