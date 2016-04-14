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
#include <errno.h>

#include "env.h"
#include "log.h"
#include "ws.h"
#include "ws_events.h"
#include "hkcp.h"
#include "mqtt.h"
#include "comm.h"


#define HAKIT_HKCP_PORT 5678   // Default HAKit HKCP communication port
#define HAKIT_HTTP_PORT 5680   // Default HAKit HTTP port

#define HAKIT_SHARE_DIR "/usr/share/hakit/"

typedef struct {
	hkcp_t hkcp;
	mqtt_t mqtt;
	ws_t *ws;
} comm_t;

static comm_t comm;


static void comm_ws_send(ws_t *ws, char *name, char *value)
{
	int size = strlen(name) + strlen(value) + 4;
	char str[size];

	/* Send WebSocket event */
	snprintf(str, size, "!%s %s", name, value);
	ws_events_send(ws, str);
}


int comm_init(int use_ssl, int use_hkcp, char *hkcp_hosts)
{
	char *path = NULL;

	memset(&comm, 0, sizeof(comm));

	/* Init HKCP gears */
	int ret = hkcp_init(&comm.hkcp, use_hkcp ? HAKIT_HKCP_PORT:0, hkcp_hosts);
	if (ret != 0) {
		return ret;
	}

	/* Search for SSL cert directory */
	if (use_ssl) {
		path = env_devdir("ssl");
		if (path != NULL) {
			log_debug(2, "Running from development environment!");
		}
		else {
			path = HAKIT_SHARE_DIR "ssl";
		}
		log_debug(2, "SSL Certficate directory: %s", path);
	}

	/* Init MQTT gears */
	if (mqtt_host != NULL) {
		if (mqtt_init(&comm.mqtt, path, (mqtt_update_func_t) hkcp_sink_update, &comm.hkcp)) {
			return -1;
		}
	}

	/* Init HTTP/WebSocket server */
	comm.ws = ws_new(HAKIT_HTTP_PORT, path);
	if (comm.ws == NULL) {
		return -1;
	}

	/* Setup document root directory stack */
	path = env_appdir("ui");
	if (path != NULL) {
		ws_add_document_root(comm.ws, path);
	}

	path = env_devdir("ui");
	if (path != NULL) {
		ws_add_document_root(comm.ws, path);
	}
	else {
		ws_add_document_root(comm.ws, HAKIT_SHARE_DIR "ui");
	}

	ws_set_command_handler(comm.ws, (ws_command_handler_t) hkcp_command, &comm.hkcp);

	return 0;
}


void comm_monitor(comm_sink_func_t func, void *user_data)
{
	hkcp_monitor(&comm.hkcp, func, user_data);
}


int comm_sink_register(char *name, comm_sink_func_t func, void *user_data)
{
	int id = hkcp_sink_register(&comm.hkcp, name);

	if (id >= 0) {
		hkcp_sink_add_handler(&comm.hkcp, id, func, user_data);
		hkcp_sink_add_handler(&comm.hkcp, id, (hkcp_sink_func_t) comm_ws_send, comm.ws);
		mqtt_subscribe(&comm.mqtt, name);
	}

	return id;
}


void comm_sink_set_local(int id)
{
	hkcp_sink_set_local(&comm.hkcp, id);
}


void comm_sink_set_widget(int id, char *widget_name)
{
	hkcp_sink_set_widget(&comm.hkcp, id, widget_name);
}


int comm_source_register(char *name, int event)
{
	return hkcp_source_register(&comm.hkcp, name, event);
}


void comm_source_set_local(int id)
{
	hkcp_source_set_local(&comm.hkcp, id);
}


void comm_source_set_widget(int id, char *widget_name)
{
	hkcp_source_set_widget(&comm.hkcp, id, widget_name);
}


void comm_source_update_str(int id, char *value)
{
	char *name = hkcp_source_update(&comm.hkcp, id, value);

	if (name != NULL) {
		int retain = hkcp_source_is_event(&comm.hkcp, id) ? 0:1;
		mqtt_publish(&comm.mqtt, name, value, retain);

		comm_ws_send(comm.ws, name, value);
	}
}


void comm_source_update_int(int id, int value)
{
	char str[32];
	snprintf(str, sizeof(str), "%d", value);
	comm_source_update_str(id, str);
}
