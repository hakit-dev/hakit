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

#ifndef __HAKIT_WS_IO_H__
#define __HAKIT_WS_IO_H__

#include <libwebsockets.h>

extern void ws_poll(struct lws_context *context, struct lws_pollargs *pa);
extern void ws_poll_remove(struct lws_pollargs *pa);
extern void ws_dump_handshake_info(struct lws *wsi);
extern void ws_show_http_token(struct lws *wsi);

#endif /* __HAKIT_WS_IO_H__ */
