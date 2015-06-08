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
#include "command.h"
#include "ws_utils.h"
#include "ws_events.h"


struct per_session_data__events {
	ws_t *ws;
	command_t *cmd;
	buf_t out_buf;
	int id;
};


static struct libwebsocket_protocols *ws_events_protocol = NULL;


static void ws_events_command(struct per_session_data__events *pss, int argc, char **argv)
{
	log_debug(2, "ws_events_command [%d]", pss->id);
	ws_call_command_handler(pss->ws, argc, argv, &pss->out_buf);
	log_debug_data(pss->out_buf.base, pss->out_buf.len);
}


static int ws_events_callback(struct libwebsocket_context *context,
			      struct libwebsocket *wsi,
			      enum libwebsocket_callback_reasons reason, void *user,
			      void *in, size_t len)
{
	ws_t *ws = libwebsocket_context_user(context);
	struct per_session_data__events *pss = user;
	int i;

	switch (reason) {
	case LWS_CALLBACK_ESTABLISHED:
		log_debug(2, "ws_events_callback LWS_CALLBACK_ESTABLISHED %p", pss);
		pss->ws = ws;
		pss->cmd = command_new((command_handler_t) ws_events_command, pss);
		buf_init(&pss->out_buf);
		pss->id = ws_session_add(ws, pss);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		log_debug(2, "ws_events_callback LWS_CALLBACK_SERVER_WRITEABLE %p", pss);
		i = pss->out_buf.len - LWS_SEND_BUFFER_PRE_PADDING;
		if (i > 0) {
			buf_grow(&pss->out_buf, LWS_SEND_BUFFER_POST_PADDING);

			int ret = libwebsocket_write(wsi, pss->out_buf.base+LWS_SEND_BUFFER_PRE_PADDING, i, LWS_WRITE_TEXT);

			pss->out_buf.len = 0;

			if (ret < i) {
				log_str("HTTP ERROR: %d writing to event websocket", ret);
				return -1;
			}
		}
		break;

	case LWS_CALLBACK_RECEIVE:
		log_debug(2, "ws_events_callback LWS_CALLBACK_RECEIVE %p", pss);
		log_debug_data(in, len);

		/* Make sure send buffer has room for pre-padding */
		if (pss->out_buf.len == 0) {
			buf_append_zero(&pss->out_buf, LWS_SEND_BUFFER_PRE_PADDING);
		}

		/* Execute command */
		command_recv(pss->cmd, in, len);

		/* Trig response write */
		if (pss->out_buf.len > LWS_SEND_BUFFER_PRE_PADDING) {
			libwebsocket_callback_on_writable(context, wsi);
		}
		break;

	case LWS_CALLBACK_CLOSED:
		log_debug(2, "ws_events_callback LWS_CALLBACK_CLOSED %p", pss);

		pss->ws = NULL;

		if (pss->cmd != NULL) {
			command_destroy(pss->cmd);
			pss->cmd = NULL;
		}

		buf_cleanup(&pss->out_buf);
		pss->id = -1;

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
	/* Make sure send buffer has room for pre-padding */
	if (pss->out_buf.len == 0) {
		buf_append_zero(&pss->out_buf, LWS_SEND_BUFFER_PRE_PADDING);
	}

	buf_append_str(&pss->out_buf, str);
}


void ws_events_send(ws_t *ws, char *str)
{
	log_debug(2, "ws_events_send '%s'", str);

	ws_session_foreach(ws, (hk_tab_foreach_func) ws_events_send_session, str);

	libwebsocket_callback_on_writable_all_protocol(ws_events_protocol);
}
