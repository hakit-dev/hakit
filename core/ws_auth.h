/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * HTTP/WebSockets authentication
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_WS_AUTH_H__
#define __HAKIT_WS_AUTH_H__

#include <libwebsockets.h>

extern int ws_auth_init(char *auth_file);
extern int ws_auth_check(struct lws *wsi, char **username);

#endif /* __HAKIT_WS_AUTH_H__ */
