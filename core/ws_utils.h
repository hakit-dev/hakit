/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_WS_UTILS_H__
#define __HAKIT_WS_UTILS_H__

#include <libwebsockets.h>

extern void ws_dump_handshake_info(struct lws *wsi);

#endif /* __HAKIT_WS_UTILS_H__ */
