/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "options.h"
#include "log.h"
#include "ws.h"
#include "ws_events.h"
#include "hkcp.h"
#include "comm.h"


#define HAKIT_HKCP_PORT 5678   // Default HAKit communication port
#define HAKIT_HTTP_PORT 5680   // Default HAKit HTTP port

static hkcp_t hk_hkcp;
static ws_t *hk_ws = NULL;


static void comm_ws_send(ws_t *ws, char *name, char *value)
{
	int size = strlen(name) + strlen(value) + 4;
	char str[size];

	/* Send WebSocket event */
	snprintf(str, size, "!%s %s", name, value);
	ws_events_send(ws, str);
}


int comm_init(void)
{
	char *path;

	/* Init HKCP gears */
	int ret = hkcp_init(&hk_hkcp, HAKIT_HKCP_PORT);
	if (ret != 0) {
		return ret;
	}

	/* Init HTTP/WebSocket server */
	hk_ws = ws_new(opt_no_hkcp ? 0:HAKIT_HTTP_PORT);
	if (hk_ws == NULL) {
		return -1;
	}

	/* Setup document root directory stack */
	path = env_appdir("ui");
	if (path != NULL) {
		ws_add_document_root(hk_ws, path);
	}

	path = env_devdir("ui");
	if (path != NULL) {
		log_debug(2, "Running from development environment!");
		ws_add_document_root(hk_ws, path);
	}
	else {
		ws_add_document_root(hk_ws, "/usr/share/hakit/ui");
	}

	ws_set_command_handler(hk_ws, (ws_command_handler_t) hkcp_command, &hk_hkcp);

	return 0;
}


void comm_monitor(comm_sink_func_t func, void *user_data)
{
	hkcp_monitor(&hk_hkcp, func, user_data);
}


int comm_sink_register(char *name, comm_sink_func_t func, void *user_data)
{
	int id = hkcp_sink_register(&hk_hkcp, name);

	if (id >= 0) {
		hkcp_sink_add_handler(&hk_hkcp, id, func, user_data);
		hkcp_sink_add_handler(&hk_hkcp, id, (hkcp_sink_func_t) comm_ws_send, hk_ws);
	}

	return id;
}


void comm_sink_set_local(int id)
{
	hkcp_sink_set_local(&hk_hkcp, id);
}


void comm_sink_set_widget(int id, char *widget_name)
{
	hkcp_sink_set_widget(&hk_hkcp, id, widget_name);
}


void comm_sink_update_str(int id, char *value)
{
	char *name = hkcp_sink_update(&hk_hkcp, id, value);

	if (name != NULL) {
		comm_ws_send(hk_ws, name, value);
	}
}


void comm_sink_update_int(int id, int value)
{
	char str[32];
	snprintf(str, sizeof(str), "%d", value);
	comm_sink_update_str(id, str);
}


int comm_source_register(char *name, int event)
{
	return hkcp_source_register(&hk_hkcp, name, event);
}


void comm_source_set_local(int id)
{
	hkcp_source_set_local(&hk_hkcp, id);
}


void comm_source_set_widget(int id, char *widget_name)
{
	hkcp_source_set_widget(&hk_hkcp, id, widget_name);
}


void comm_source_update_str(int id, char *value)
{
	char *name = hkcp_source_update(&hk_hkcp, id, value);

	if (name != NULL) {
		comm_ws_send(hk_ws, name, value);
	}
}


void comm_source_update_int(int id, int value)
{
	char str[32];
	snprintf(str, sizeof(str), "%d", value);
	comm_source_update_str(id, str);
}
