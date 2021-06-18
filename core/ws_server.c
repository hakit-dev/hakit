/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2021 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <malloc.h>
#include <libwebsockets.h>

#include "lws_config.h"
#include "options.h"
#include "log.h"
#include "tab.h"
#include "ws_log.h"
#include "ws_io.h"
#include "ws_http.h"
#include "ws_events.h"
#include "ws_server.h"


/*
 * Table of available protocols
 */

static struct lws_protocols ws_server_protocols[] = {
	{ }, /* first protocol must always be HTTP handler */
	{ }, /* hakit-events-protocol */
	{ NULL, NULL, 0, 0 } /* terminator */
};


/*
 * HTTP/WebSocket server init
 */

int ws_server_init(ws_server_t *server, int port, char *ssl_dir)
{
#ifdef WITH_SSL
	int ssl_dir_len = ssl_dir ? strlen(ssl_dir) : 0;
	char cert_path[ssl_dir_len+16];
	char key_path[ssl_dir_len+16];
#endif
	struct lws_context_creation_info info;

	log_str("Using libwebsockets version " LWS_LIBRARY_VERSION " build " LWS_BUILD_HASH);

	// Setup LWS logging
	ws_log_init(opt_debug);

	ws_http_init(&ws_server_protocols[0]);
	ws_events_init(&ws_server_protocols[1]);

	memset(&info, 0, sizeof(info));
	info.port = port;
	info.protocols = ws_server_protocols;
	//info.extensions = lws_get_internal_extensions();

#ifdef WITH_SSL
	/* Setup server SSL info */
	if (ssl_dir != NULL) {
		snprintf(cert_path, sizeof(cert_path), "%s/cert.pem", ssl_dir);
		info.ssl_cert_filepath = cert_path;

		snprintf(key_path, sizeof(key_path), "%s/privkey.pem", ssl_dir);
		info.ssl_private_key_filepath = key_path;

		log_debug(2, "SSL info: cert='%s' key='%s'", cert_path, key_path);

		info.options |= LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS;
	}
#endif

	info.gid = -1;
	info.uid = -1;
	info.user = server;

	/* Create libwebsockets context */
	server->context = lws_create_context(&info);
	if (server->context == NULL) {
		return -1;
	}

	/* Init table of document root directories */
	hk_tab_init(&server->document_roots, sizeof(char *));

	/* Init table of aliases */
	hk_tab_init(&server->aliases, sizeof(ws_alias_t));

	/* Init table of websocket sessions */
	hk_tab_init(&server->sessions, sizeof(void *));

	return 0;
}


void ws_server_destroy(ws_server_t *server)
{
	int i;

	/* Free document root directory list */
	for (i = 0; i < server->document_roots.nmemb; i++) {
		char **p = HK_TAB_PTR(server->document_roots, char *, i);
		free(*p);
		*p = NULL;
	}
	hk_tab_cleanup(&server->document_roots);
}


/*
 * HTTP directory and aliased locations
 */

void ws_add_document_root(ws_server_t *server, char *dir)
{
        int i;

	log_debug(2, "ws_add_document_root '%s'", dir);

	for (i = 0; i < server->document_roots.nmemb; i++) {
		char *dir0 = HK_TAB_VALUE(server->document_roots, char *, i);
                if (strcmp(dir0, dir) == 0) {
                        log_debug(2, "  -> Already exists");
                        return;
                }
        }

	char **p = hk_tab_push(&server->document_roots);
	*p = strdup(dir);

        log_debug(2, "  -> Added");
}


void ws_alias(ws_server_t *server, char *location, ws_alias_handler_t handler, void *user_data)
{
	ws_alias_t *alias = hk_tab_push(&server->aliases);

	if (location != NULL) {
		alias->location = strdup(location);
		alias->len = strlen(location);
	}

	alias->handler = handler;
	alias->user_data = user_data;

	log_debug(2, "ws_alias '%s'", location);
}


/*
 * WebSocket running sessions
 */

int ws_session_add(ws_server_t *server, void *pss)
{
	void **ppss = NULL;
	int i;

	server->salt++;
	server->salt &= 0xFF;

	for (i = 0; i < server->sessions.nmemb; i++) {
		ppss = HK_TAB_PTR(server->sessions, void *, i);
		if (*ppss == NULL) {
			goto done;
		}
	}

	ppss = hk_tab_push(&server->sessions);
done:
	*ppss = pss;

	return (server->salt << 8) + (i & 0xFF);
}


void ws_session_remove(ws_server_t *server, void *pss)
{
	void **ppss = NULL;
	int i;

	for (i = 0; i < server->sessions.nmemb; i++) {
		ppss = HK_TAB_PTR(server->sessions, void *, i);
		if (*ppss == pss) {
			*ppss = NULL;
		}
	}
}


void ws_session_foreach(ws_server_t *server, ws_session_foreach_func func, void *user_data)
{
	int i;

	for (i = 0; i < server->sessions.nmemb; i++) {
		void *pss = HK_TAB_VALUE(server->sessions, void *, i);
		if (pss != NULL) {
			if (func != NULL) {
				func(user_data, pss);
			}
		}
	}
}


/*
 * WebSocket receive event
 */

void ws_server_set_command_handler(ws_server_t *server, ws_command_handler_t handler, void *user_data)
{
	server->command_handler = handler;
	server->command_user_data = user_data;
}


void ws_server_receive_event(ws_server_t *server, int argc, char **argv, buf_t *out_buf)
{
	if (server->command_handler != NULL) {
		server->command_handler(server->command_user_data, argc, argv, out_buf);
	}
}


/*
 * WebSocket send event
 */

void ws_server_send_event(ws_server_t *server, char *str)
{
        ws_events_send(server, str);
}
