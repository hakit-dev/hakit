/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Demo for libwebsockets server test
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "sys.h"
#include "ws_utils.h"
#include "ws_demo.h"


struct per_session_data__demo {
	int number;
	int update;
	sys_tag_t tag;
};


static struct libwebsocket_protocols *ws_demo_protocol = NULL;


static void ws_demo_writeable(struct per_session_data__demo *pss)
{
	pss->update = 1;
	libwebsocket_callback_on_writable_all_protocol(ws_demo_protocol);

}


static int ws_demo_tick(struct per_session_data__demo *pss)
{
	log_debug(2, "ws_demo_tick number=%d", pss->number);
	pss->number++;
	ws_demo_writeable(pss);
	return 1;
}


static int ws_demo_callback(struct libwebsocket_context *context,
			    struct libwebsocket *wsi,
			    enum libwebsocket_callback_reasons reason, void *user,
			    void *in, size_t len)
{
	//ws_t *ws = libwebsocket_context_user(context);
	struct per_session_data__demo *pss = user;
	int n, m;
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + 512 + LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];


	switch (reason) {
	case LWS_CALLBACK_ESTABLISHED:
		log_debug(2, "ws_demo_callback LWS_CALLBACK_ESTABLISHED %p", pss);
		pss->number = 0;
		pss->update = 0;
		pss->tag = sys_timeout(1000, (sys_func_t) ws_demo_tick, pss);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		if (pss->update) {
			log_debug(2, "ws_demo_callback LWS_CALLBACK_SERVER_WRITEABLE %p", pss);
			n = sprintf((char *)p, "%d", pss->number);
			m = libwebsocket_write(wsi, p, n, LWS_WRITE_TEXT);
			if (m < n) {
				log_str("HTTP ERROR: %d writing to di socket", n);
				return -1;
			}
			pss->update = 0;
		}
		break;

	case LWS_CALLBACK_RECEIVE:
		log_debug(2, "ws_demo_callback LWS_CALLBACK_RECEIVE %p", pss);
		log_debug_data(in, len);

		if (len >= 6) {
			if (strcmp((const char *)in, "reset\n") == 0) {
				pss->number = 0;
				ws_demo_writeable(pss);
			}
		}
		break;

	case LWS_CALLBACK_CLOSED:
		log_debug(2, "ws_demo_callback LWS_CALLBACK_CLOSED %p", pss);
		sys_remove(pss->tag);
		pss->tag = 0;
		break;

	/*
	 * this just demonstrates how to use the protocol filter. If you won't
	 * study and reject connections based on header content, you don't need
	 * to handle this callback
	 */
	case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
		log_debug(3, "ws_demo_callback LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION");
		ws_dump_handshake_info(wsi);
		/* you could return non-zero here and kill the connection */
		break;

	default:
		log_debug(3, "ws_demo_callback: reason=%d", reason);
		break;
	}

	return 0;
}


void ws_demo_init(struct libwebsocket_protocols *protocol)
{
	ws_demo_protocol = protocol;

	protocol->name = "dumb-increment-protocol";
	protocol->callback = ws_demo_callback;
	protocol->per_session_data_size = sizeof(struct per_session_data__demo);
	protocol->rx_buffer_size = 10;
}
