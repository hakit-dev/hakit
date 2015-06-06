/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_WS_H__
#define __HAKIT_WS_H__

#include "buf.h"
#include "tab.h"

typedef struct ws_s ws_t;
typedef void (*ws_alias_handler_t)(void *user_data, char *uri, buf_t *rsp);

typedef struct {
	char *location;
	int len;
	ws_alias_handler_t handler;
	void *user_data;
} ws_alias_t;

struct ws_s {
	void *context;
	char *document_root;
	int document_root_len;
	hk_tab_t aliases;       // Table of (ws_alias_t)
	hk_tab_t sessions;      // Table of WebSocket sessions (void *)
};


extern ws_t *ws_new(int port);
extern void ws_destroy(ws_t *ws);

extern void ws_document_root(ws_t *ws, char *document_root);
extern void ws_alias(ws_t *ws, char *location, ws_alias_handler_t handler, void *user_data);

/* WebSocket session list management */
extern void ws_session_add(ws_t *ws, void *pss);
extern void ws_session_remove(ws_t *ws, void *pss);
extern void ws_session_foreach(ws_t *ws, hk_tab_foreach_func func, char *user_data);

#endif /* __HAKIT_WS_H__ */
