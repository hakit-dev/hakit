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
#include <libwebsockets.h>

#include "log.h"
#include "buf.h"
#include "tab.h"
#include "command.h"
#include "ws_server.h"
#include "ws_io.h"
#include "ws_auth.h"
#include "ws_events.h"


struct per_session_data__events {
	ws_server_t *server;
	command_t *cmd;
	buf_t out_buf;
	int id;
};


static struct lws_protocols *ws_events_protocol = NULL;


static void ws_events_command(struct per_session_data__events *pss, int argc, char **argv)
{
	log_debug(2, "ws_events_command [%04X]: '%s'%s", pss->id, argv[0], (argc > 1) ? " ...":"");
	ws_server_receive_event(pss->server, argc, argv, &pss->out_buf);
	log_debug_data(pss->out_buf.base, pss->out_buf.len);
}


static int ws_events_callback(struct lws *wsi,
			      enum lws_callback_reasons reason, void *user,
			      void *in, size_t len)
{
	struct lws_context *context = lws_get_context(wsi);
	ws_server_t *server = lws_context_user(context);
	struct per_session_data__events *pss = user;
	int ret = 0;
	int i;

	switch (reason) {
	case LWS_CALLBACK_ESTABLISHED:
		pss->server = server;
		pss->cmd = command_new((command_handler_t) ws_events_command, pss);
		buf_init(&pss->out_buf);
		pss->id = ws_session_add(server, pss);
		log_debug(2, "ws_events_callback LWS_CALLBACK_ESTABLISHED [%04X]", pss->id);

		//ws_show_http_token(wsi);

		if (!ws_auth_check(wsi, NULL)) {
			ret = -1;
		}
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		i = pss->out_buf.len - LWS_SEND_BUFFER_PRE_PADDING;
		if (i > 0) {
			log_debug(2, "ws_events_callback LWS_CALLBACK_SERVER_WRITEABLE [%04X]: %d bytes", pss->id, i);
			buf_grow(&pss->out_buf, LWS_SEND_BUFFER_POST_PADDING);

			int ret = lws_write(wsi, pss->out_buf.base+LWS_SEND_BUFFER_PRE_PADDING, i, LWS_WRITE_TEXT);

			pss->out_buf.len = 0;

			if (ret < i) {
				log_str("WS ERROR: %d writing to event websocket", ret);
				ret = -1;
			}
		}
		else {
			log_debug(2, "ws_events_callback LWS_CALLBACK_SERVER_WRITEABLE [%04X]: READY", pss->id);
		}
		break;

	case LWS_CALLBACK_RECEIVE:
		log_debug(2, "ws_events_callback LWS_CALLBACK_RECEIVE [%04X]: %d bytes", pss->id, len);
		log_debug_data(in, len);

		/* Make sure send buffer has room for pre-padding */
		if (pss->out_buf.len == 0) {
			buf_append_zero(&pss->out_buf, LWS_SEND_BUFFER_PRE_PADDING);
		}

		/* Execute command */
		command_recv(pss->cmd, in, len);

		/* Trig response write */
		lws_callback_on_writable(wsi);
		break;

	case LWS_CALLBACK_CLOSED:
		log_debug(2, "ws_events_callback LWS_CALLBACK_CLOSED [%04X]", pss->id);

		pss->server = NULL;

		if (pss->cmd != NULL) {
			command_destroy(pss->cmd);
			pss->cmd = NULL;
		}

		buf_cleanup(&pss->out_buf);
		pss->id = -1;

		ws_session_remove(server, pss);
		break;

	/*
	 * this just demonstrates how to use the protocol filter. If you won't
	 * study and reject connections based on header content, you don't need
	 * to handle this callback
	 */
	case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
		log_debug(2, "ws_events_callback LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION [%04X]", pss->id);
		ws_dump_handshake_info(wsi);
		/* you could return non-zero here and kill the connection */
//		return -1;
		break;

	case LWS_CALLBACK_PROTOCOL_INIT:
		log_debug(2, "ws_events_callback LWS_CALLBACK_PROTOCOL_INIT");
		break;

	default:
		log_debug(2, "ws_events_callback: reason=%d", reason);
		break;
	}

	return ret;
}


void ws_events_init(struct lws_protocols *protocol)
{
	ws_events_protocol = protocol;

	protocol->name = "hakit-events-protocol";
	protocol->callback = ws_events_callback;
	protocol->per_session_data_size = sizeof(struct per_session_data__events);
	protocol->rx_buffer_size = 256;
}


static void ws_events_send_session(char *str, struct per_session_data__events *pss)
{
	log_debug(2, "ws_events_send_session  [%04X] '%s'", pss->id, str);

	/* Make sure send buffer has room for pre-padding */
	if (pss->out_buf.len == 0) {
		buf_append_zero(&pss->out_buf, LWS_SEND_BUFFER_PRE_PADDING);
	}

	buf_append_str(&pss->out_buf, str);
	buf_append_byte(&pss->out_buf, '\n');
}


void ws_events_send(ws_server_t *server, char *str)
{
	log_debug(2, "ws_events_send '%s'", str);

	ws_session_foreach(server, (hk_tab_foreach_func) ws_events_send_session, str);

	lws_callback_on_writable_all_protocol(server->context, ws_events_protocol);
}
