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

#include "sys.h"
#include "buf.h"
#include "tab.h"
#include "ws_client.h"

typedef void (*ws_alias_handler_t)(void *user_data, char *uri, buf_t *rsp);
typedef void (*ws_command_handler_t)(void *user_data, int argc, char **argv, buf_t *out_buf);

typedef struct {
	char *location;
	int len;
	ws_alias_handler_t handler;
	void *user_data;
} ws_alias_t;

typedef struct {
	void *context;
	hk_tab_t document_roots; // Table of (char *)
	hk_tab_t aliases;       // Table of (ws_alias_t)
	hk_tab_t sessions;      // Table of WebSocket sessions (void *)
	ws_command_handler_t command_handler;
	void *command_user_data;
	int salt;
        sys_tag_t tick;
} ws_server_t;

typedef struct {
	ws_client_t client;
	ws_server_t server;
} ws_t;


extern ws_t *ws_new(int port, int use_ssl, char *ssl_dir);
extern void ws_destroy(ws_t *ws);

/* HTTP server configuration */
extern void ws_add_document_root(ws_t *ws, char *dir);
extern void ws_alias(ws_t *ws, char *location, ws_alias_handler_t handler, void *user_data);

/* WebSocket command handling */
extern void ws_set_command_handler(ws_t *ws, ws_command_handler_t handler, void *user_data);
extern void ws_call_command_handler(ws_t *ws, int argc, char **argv, buf_t *out_buf);

/* WebSocket session list management */
typedef int (*ws_session_foreach_func)(void * user_data, void *pss);
extern int ws_session_add(ws_t *ws, void *pss);
extern void ws_session_remove(ws_t *ws, void *pss);
extern void ws_session_foreach(ws_t *ws, ws_session_foreach_func func, void *user_data);

/* HTTP client */
extern int ws_client(ws_t *ws, char *uri);

#endif /* __HAKIT_WS_H__ */
