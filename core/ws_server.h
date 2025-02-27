/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2021 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_WS_SERVER_H__
#define __HAKIT_WS_SERVER_H__

#include "buf.h"
#include "tab.h"

typedef void (*ws_command_handler_t)(void *user_data, int argc, char **argv, buf_t *out_buf);

typedef struct {
	char *location;
	int len;
        char *dir;
} ws_alias_t;

typedef struct {
	void *context;
	hk_tab_t document_roots; // Table of (char *)
	hk_tab_t aliases;       // Table of (ws_alias_t)
	hk_tab_t sessions;      // Table of WebSocket sessions (void *)
	ws_command_handler_t command_handler;
	void *command_user_data;
	int salt;
} ws_server_t;


extern int ws_server_init(ws_server_t *server, int port, char *ssl_dir);
extern void ws_server_destroy(ws_server_t *server);

/* HTTP server configuration */
extern void ws_add_document_root(ws_server_t *server, char *dir);
extern void ws_alias(ws_server_t *server, char *location, char *dir);

/* WebSocket command handling */
extern void ws_server_set_command_handler(ws_server_t *server, ws_command_handler_t handler, void *user_data);
extern void ws_server_receive_event(ws_server_t *server, int argc, char **argv, buf_t *out_buf);
extern void ws_server_send_event(ws_server_t *server, char *str);

/* WebSocket session list management */
typedef int (*ws_session_foreach_func)(void * user_data, void *pss);
extern int ws_session_add(ws_server_t *server, void *pss);
extern void ws_session_remove(ws_server_t *server, void *pss);
extern void ws_session_foreach(ws_server_t *server, ws_session_foreach_func func, void *user_data);

#endif /* __HAKIT_WS_SERVER_H__ */
