/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * LWS i/o management
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <poll.h>
#include <libwebsockets.h>

#include "types.h"
#include "log.h"
#include "sys.h"
#include "ws_io.h"


static int ws_callback_poll(struct lws_context *context, struct pollfd *pollfd)
{
        int ret = (pollfd->revents & (POLLHUP|POLLNVAL)) ? 0:1;
	int err;

	log_debug(3, "ws_callback_poll: fd=%d revents=%02X", pollfd->fd, pollfd->revents);

	err = lws_service_fd(context, pollfd);
	if (err < 0) {
		log_str("PANIC: lws service returned error %d", err);
                return 0;
	}

        /* if needed, force-service wsis that may not have read all input */
        while (!lws_service_adjust_timeout(context, 1, 0)) {
                lwsl_notice("extpoll doing forced service!\n");
                lws_service_tsi(context, -1, 0);
        }

	return ret;
}


void ws_poll(struct lws_context *context, struct lws_pollargs *pa)
{
	sys_io_poll(pa->fd, pa->events, (sys_poll_func_t) ws_callback_poll, context);
}


void ws_poll_remove(struct lws_pollargs *pa)
{
	sys_remove_fd(pa->fd);
}


void ws_dump_handshake_info(struct lws *wsi)
{
	int n = 0;
	char buf[256];
	const unsigned char *c;

	do {
		c = lws_token_to_string(n);
		if (!c) {
			n++;
			continue;
		}

		if (!lws_hdr_total_length(wsi, n)) {
			n++;
			continue;
		}

		lws_hdr_copy(wsi, buf, sizeof buf, n);

//		fprintf(stderr, "    %s = %s\n", (char *)c, buf);  //REVISIT
		n++;
	} while (c);
}


void ws_show_http_token(struct lws *wsi)
{
	int i;

	/* Show all token */
	for (i = 0; i < WSI_TOKEN_COUNT; i++) {
		int len = lws_hdr_total_length(wsi, i);
		if (len > 0) {
			char str[len+1];
			lws_hdr_copy(wsi, str, sizeof(str), i);
			log_debug(2, "ws_http_request TOKEN: %s %s", lws_token_to_string(i), str);
		}
	}
}
