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
typedef void (*ws_command_handler_t)(void *user_data, int argc, char **argv, buf_t *out_buf);

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
	ws_command_handler_t command_handler;
	void *command_user_data;
	int salt;
};


extern ws_t *ws_new(int port);
extern void ws_destroy(ws_t *ws);

/* HTTP server configuration */
extern void ws_set_document_root(ws_t *ws, char *document_root);
extern void ws_alias(ws_t *ws, char *location, ws_alias_handler_t handler, void *user_data);

/* WebSocket command handling */
extern void ws_set_command_handler(ws_t *ws, ws_command_handler_t handler, void *user_data);
extern void ws_call_command_handler(ws_t *ws, int argc, char **argv, buf_t *out_buf);

/* WebSocket session list management */
typedef int (*ws_session_foreach_func)(void * user_data, void *pss);
extern int ws_session_add(ws_t *ws, void *pss);
extern void ws_session_remove(ws_t *ws, void *pss);
extern void ws_session_foreach(ws_t *ws, ws_session_foreach_func func, void *user_data);

#endif /* __HAKIT_WS_H__ */
