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
#include "options.h"
#include "log.h"
#include "io.h"
#include "ws.h"
#include "ws_events.h"
#include "ws_client.h"
#include "hkcp.h"
#include "eventq.h"
#include "command.h"
#include "comm.h"


#define HAKIT_HKCP_PORT 5678   // Default HAKit communication port
#define HAKIT_HTTP_PORT 5680   // Default HAKit HTTP port

#define HAKIT_SHARE_DIR "/usr/share/hakit/"

static hkcp_t comm_hkcp;
static ws_t *comm_ws = NULL;
static io_channel_t comm_stdin;


static void comm_ws_send(ws_t *ws, char *name, char *value)
{
	int size = strlen(name) + strlen(value) + 4;
	char str[size];

	/* Send WebSocket event */
	snprintf(str, size, "!%s %s", name, value);
	ws_events_send(ws, str);
}


static void comm_wget_recv(void *user_data, char *buf, int len)
{
	fwrite(buf, 1, len, stdout);
}


static void comm_command_stdin(hkcp_t *hkcp, int argc, char **argv)
{
	buf_t out_buf;

	if (argc > 0) {
		buf_init(&out_buf);

		if (strcmp(argv[0], "wget") == 0) {
			if (argc > 1) {
				// HTTP/HTTPS get operation. This command is for debug/testing purpose only.
				// Result will be displayed to the debug log
				ws_client_get(&comm_ws->client, argv[1], NULL, comm_wget_recv, NULL);
			}
			else {
				log_str("ERROR: Usage: %s <uri>", argv[0]);
			}
		}
		else if (strcmp(argv[0], "eventq") == 0) {
			if (argc > 1) {
				int argi = 1;

				// HTTP/HTTPS event push. This command is for debug/testing purpose only.
				// Result will be displayed to the debug log
				if (argc > 2) {
					eventq_init(argv[argi++]);
				}
				eventq_push(argv[argi]);
			}
			else {
				log_str("ERROR: Usage: %s [<uri>] <event>", argv[0]);
			}
		}
		else {
			hkcp_command(&comm_hkcp, argc, argv, &out_buf);
			if (fwrite(out_buf.base, 1, out_buf.len, stdout) < 0) {
				log_str("PANIC: Failed to write stdout: %s", strerror(errno));
			}
		}

		buf_cleanup(&out_buf);
	}
	else {
		/* Quit if hangup from stdin */
		sys_quit();
	}
}


int comm_init(int use_ssl, int use_hkcp, char *hkcp_hosts)
{
	char *path = NULL;

	/* Init HKCP gears */
	int ret = hkcp_init(&comm_hkcp, use_hkcp ? HAKIT_HKCP_PORT:0, hkcp_hosts);
	if (ret != 0) {
		return ret;
	}

	/* Search for SSL cert directory */
	if (use_ssl) {
		path = env_devdir("ssl");
		if (path == NULL) {
			path = strdup(HAKIT_SHARE_DIR "ssl");
		}
		log_debug(2, "SSL Certficate directory: %s", path);
	}

	/* Init HTTP/WebSocket server */
	comm_ws = ws_new(HAKIT_HTTP_PORT, use_ssl, path);
        if (path != NULL) {
                free(path);
        }
	if (comm_ws == NULL) {
		return -1;
	}

	/* Setup document root directory stack */
	path = env_appdir("ui");
	if (path != NULL) {
		ws_add_document_root(comm_ws, path);
                free(path);
	}

	path = env_devdir("ui");
	if (path != NULL) {
		log_debug(2, "Running from development environment!");
	}
	else {
		path = strdup(HAKIT_SHARE_DIR "ui");
	}

        ws_add_document_root(comm_ws, path);
        free(path);

	ws_set_command_handler(comm_ws, (ws_command_handler_t) hkcp_command, &comm_hkcp);

	/* Setup stdin command handler if not running as a daemon */
	if (!opt_daemon) {
		command_t *cmd = command_new((command_handler_t) comm_command_stdin, &comm_hkcp);
		io_channel_setup(&comm_stdin, fileno(stdin), (io_func_t) command_recv, cmd);
	}

	return 0;
}


void comm_monitor(comm_sink_func_t func, void *user_data)
{
	hkcp_monitor(&comm_hkcp, func, user_data);
}


int comm_sink_register(char *name, comm_sink_func_t func, void *user_data)
{
	int id = hkcp_sink_register(&comm_hkcp, name);

	if (id >= 0) {
		hkcp_sink_add_handler(&comm_hkcp, id, func, user_data);
		hkcp_sink_add_handler(&comm_hkcp, id, (hkcp_sink_func_t) comm_ws_send, comm_ws);
	}

	return id;
}


void comm_sink_set_local(int id)
{
	hkcp_sink_set_local(&comm_hkcp, id);
}


void comm_sink_set_widget(int id, char *widget_name)
{
	hkcp_sink_set_widget(&comm_hkcp, id, widget_name);
}


void comm_sink_update_str(int id, char *value)
{
	char *name = hkcp_sink_update(&comm_hkcp, id, value);

	if (name != NULL) {
		comm_ws_send(comm_ws, name, value);
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
	return hkcp_source_register(&comm_hkcp, name, event);
}


void comm_source_set_local(int id)
{
	hkcp_source_set_local(&comm_hkcp, id);
}


void comm_source_set_widget(int id, char *widget_name)
{
	hkcp_source_set_widget(&comm_hkcp, id, widget_name);
}


void comm_source_update_str(int id, char *value)
{
	char *name = hkcp_source_update(&comm_hkcp, id, value);

	if (name != NULL) {
		comm_ws_send(comm_ws, name, value);
	}
}


void comm_source_update_int(int id, int value)
{
	char str[32];
	snprintf(str, sizeof(str), "%d", value);
	comm_source_update_str(id, str);
}


int comm_wget(char *uri, comm_recv_func_t *func, void *user_data)
{
	return ws_client_get(&comm_ws->client, uri, NULL, func, user_data);
}
