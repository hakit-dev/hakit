/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * WebSocket HAKit events
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>

#include "log.h"
#include "sys.h"
#include "buf.h"
#include "ws_utils.h"
#include "ws_events.h"


struct per_session_data__events {
	buf_t buf;
};


static struct libwebsocket_protocols *ws_events_protocol = NULL;


static int ws_events_callback(struct libwebsocket_context *context,
			      struct libwebsocket *wsi,
			      enum libwebsocket_callback_reasons reason, void *user,
			      void *in, size_t len)
{
	ws_t *ws = libwebsocket_context_user(context);
	struct per_session_data__events *pss = user;
	int buflen;

	switch (reason) {
	case LWS_CALLBACK_ESTABLISHED:
		log_debug(2, "ws_events_callback LWS_CALLBACK_ESTABLISHED %p", pss);
		buf_init(&pss->buf);
		ws_session_add(ws, pss);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		log_debug(2, "ws_events_callback LWS_CALLBACK_SERVER_WRITEABLE %p", pss);
		buflen = pss->buf.len - LWS_SEND_BUFFER_PRE_PADDING;
		if (buflen > 0) {
			buf_grow(&pss->buf, LWS_SEND_BUFFER_POST_PADDING);

			int ret = libwebsocket_write(wsi, pss->buf.base+LWS_SEND_BUFFER_PRE_PADDING, buflen, LWS_WRITE_TEXT);

			pss->buf.len = 0;

			if (ret < buflen) {
				log_str("HTTP ERROR: %d writing to event websocket", ret);
				return -1;
			}
		}
		break;

	case LWS_CALLBACK_RECEIVE:
		log_debug(2, "ws_events_callback LWS_CALLBACK_RECEIVE %p", pss);
		log_debug_data(in, len);

		//TODO: handle command
		break;

	case LWS_CALLBACK_CLOSED:
		log_debug(2, "ws_events_callback LWS_CALLBACK_CLOSED %p", pss);
		buf_cleanup(&pss->buf);
		ws_session_remove(ws, pss);
		break;

	/*
	 * this just demonstrates how to use the protocol filter. If you won't
	 * study and reject connections based on header content, you don't need
	 * to handle this callback
	 */
	case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
		log_debug(2, "ws_events_callback LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION");
		ws_dump_handshake_info(wsi);
		/* you could return non-zero here and kill the connection */
		break;

	default:
		log_debug(2, "ws_events_callback: reason=%d", reason);
		break;
	}

	return 0;
}


void ws_events_init(struct libwebsocket_protocols *protocol)
{
	ws_events_protocol = protocol;

	protocol->name = "hakit-events-protocol";
	protocol->callback = ws_events_callback;
	protocol->per_session_data_size = sizeof(struct per_session_data__events);
	protocol->rx_buffer_size = 64;
}


static void ws_events_send_session(char *str, struct per_session_data__events *pss)
{
	if (pss->buf.len == 0) {
		buf_append_zero(&pss->buf, LWS_SEND_BUFFER_PRE_PADDING);
	}
	buf_append_str(&pss->buf, str);
}


void ws_events_send(ws_t *ws, char *str)
{
	log_debug(2, "ws_events_send '%s'", str);

	ws_session_foreach(ws, (hk_tab_foreach_func) ws_events_send_session, str);

	libwebsocket_callback_on_writable_all_protocol(ws_events_protocol);
}
