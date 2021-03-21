/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2021 Sylvain Giroudon
 *
 * HTTP server
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_WS_HTTP_H__
#define __HAKIT_WS_HTTP_H__

#include <libwebsockets.h>

extern void ws_http_init(struct lws_protocols *protocol);

#endif /* __HAKIT_WS_HTTP_H__ */
