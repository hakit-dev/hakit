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

#ifndef __HAKIT_WS_EVENTS_H__
#define __HAKIT_WS_EVENTS_H__

#include <libwebsockets.h>
#include "ws.h"

extern void ws_events_init(struct libwebsocket_protocols *protocol);

extern void ws_events_send(ws_t *ws, char *str);

#endif /* __HAKIT_WS_EVENTS_H__ */
