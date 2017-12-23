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
#include "files.h"
#include "io.h"
#include "ws.h"
#include "ws_events.h"
#include "ws_client.h"
#include "hkcp.h"
#include "hkcp_cmd.h"
#include "mqtt.h"
#include "eventq.h"
#include "command.h"
#include "endpoint.h"
#include "advertise.h"
#include "mod_load.h"
#include "mod.h"
#include "comm.h"


#define HAKIT_HKCP_PORT 5678   // Default HAKit HKCP communication port
#define HAKIT_HTTP_PORT 5680   // Default HAKit HTTP port

#define HAKIT_SHARE_DIR "/usr/share/hakit/"

typedef struct {
	hk_endpoints_t eps;
	hk_advertise_t adv;
	int use_hkcp;
	hkcp_t hkcp;
#ifdef WITH_MQTT
	mqtt_t mqtt;
#endif
	ws_t *ws;
	io_channel_t io_stdin;
} comm_t;

static comm_t comm;


static void comm_ws_send(ws_t *ws, hk_ep_t *ep)
{
        char *tile_name = hk_ep_get_tile_name(ep);
        char *name = hk_ep_get_name(ep);
        char *value = hk_ep_get_value(ep);
	int size = strlen(tile_name) + strlen(name) + strlen(value) + 4;
	char str[size];

	/* Send WebSocket event */
        if (hk_tile_nmemb() > 1) {
                snprintf(str, size, "!%s.%s=%s", tile_name, name, value);
        }
        else {
                snprintf(str, size, "!%s=%s", name, value);
        }

	ws_events_send(ws, str);
}


#ifdef WITH_MQTT

static void comm_mqtt_publish(mqtt_t *mqtt, hk_ep_t *ep)
{
	mqtt_publish(mqtt, hk_ep_get_name(ep), hk_ep_get_value(ep),  hk_ep_flag_retain(ep));
}


static void comm_mqtt_subscribe(mqtt_t *mqtt, hk_ep_t *ep)
{
	mqtt_subscribe(mqtt, hk_ep_get_name(ep));
}


static void comm_mqtt_connected(comm_t *comm)
{
	hk_source_foreach_public(&comm->eps, (hk_ep_func_t) comm_mqtt_publish, &comm->mqtt);
	hk_sink_foreach_public(&comm->eps, (hk_ep_func_t) comm_mqtt_subscribe, &comm->mqtt);
}


static void comm_mqtt_update(comm_t *comm, char *name, char *value)
{
	if (name != NULL) {
		log_debug(2, "comm_mqtt_update %s='%s'", name, value);
		hk_sink_update_by_name(&comm->eps, name, value);
	}
	else {
		log_debug(2, "comm_mqtt_update CONNECTED");
		comm_mqtt_connected(comm);
	}
}

static void comm_mqtt_discover(comm_t *comm, char *remote_ip, char *broker)
{
	log_debug(2, "comm_mqtt_discover %s '%s'", remote_ip, broker);
	if (broker != NULL) {
		mqtt_connect(&comm->mqtt, broker);
	}
}

#endif /* WITH_MQTT */


static void comm_wget_recv(void *user_data, char *buf, int len)
{
	fwrite(buf, 1, len, stdout);
}


static void comm_command_stdin(comm_t *comm, int argc, char **argv)
{
	buf_t out_buf;

	if (argc > 0) {
		buf_init(&out_buf);

		if (strcmp(argv[0], "wget") == 0) {
			if (argc > 1) {
				// HTTP/HTTPS get operation. This command is for debug/testing purpose only.
				// Result will be displayed to the debug log
				ws_client_get(&comm->ws->client, argv[1], NULL, comm_wget_recv, NULL);
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
			hkcp_command(&comm->hkcp, argc, argv, &out_buf);
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


int comm_init(int use_ssl, char *certs,
              int use_hkcp,
              int use_mqtt, char *mqtt_broker)
{
	char *path = NULL;
	int ret = 0;

	memset(&comm, 0, sizeof(comm));

	/* Init endpoint management */
	hk_endpoints_init(&comm.eps);

	/* Init advertising protocol */
	if (hk_advertise_init(&comm.adv, HAKIT_HKCP_PORT)) {
		ret = -1;
		goto DONE;
	}

	/* Init HKCP gears */
	comm.use_hkcp = use_hkcp;
	ret = hkcp_init(&comm.hkcp, &comm.eps, use_hkcp ? HAKIT_HKCP_PORT:0, certs);
	if (ret != 0) {
		goto DONE;
	}

	if (use_hkcp) {
		/* Handle HKCP advertisement */
		hk_advertise_handler(&comm.adv, ADVERTISE_PROTO_HKCP, (hk_advertise_func_t) hkcp_node_add, &comm.hkcp);
	}

#ifdef WITH_MQTT
        if (use_mqtt) {
                /* Init MQTT gears */
                if (mqtt_init(&comm.mqtt, certs, (mqtt_update_func_t) comm_mqtt_update, &comm)) {
                        ret = -1;
                        goto DONE;
                }

                if (mqtt_broker != NULL) {
                        /* Connect to broker */
                        if (mqtt_connect(&comm.mqtt, mqtt_broker)) {
                                ret = -1;
                                goto DONE;
                        }
                }
                else {
                        /* Handle MQTT advertisement */
                        hk_advertise_handler(&comm.adv, ADVERTISE_PROTO_MQTT, (hk_advertise_func_t) comm_mqtt_discover, &comm);
                }

                /* Advertise MQTT protocol */
                hk_advertise_mqtt(&comm.adv, mqtt_broker);
        }
#endif /* WITH_MQTT */

	/* Search for SSL cert directory */
	if (use_ssl) {
		path = env_devdir("ssl");
		if (path == NULL) {
			path = strdup(HAKIT_SHARE_DIR "ssl");
		}
		log_debug(2, "SSL Certficate directory: %s", path);
	}

	/* Init HTTP/WebSocket server */
	comm.ws = ws_new(HAKIT_HTTP_PORT, use_ssl, path);
        if (path != NULL) {
                free(path);
        }
	if (comm.ws == NULL) {
		ret = -1;
		goto DONE;
	}

	/* Setup document root directory stack */
	path = env_devdir("ui");
	if (path != NULL) {
		log_debug(2, "Running from development environment!");
	}
	else {
		path = strdup(HAKIT_SHARE_DIR "ui");
	}

        ws_add_document_root(comm.ws, path);
        free(path);

	ws_set_command_handler(comm.ws, (ws_command_handler_t) hkcp_command, &comm.hkcp);

	/* Setup stdin command handler if not running as a daemon */
	if (!opt_daemon) {
		command_t *cmd = command_new((command_handler_t) comm_command_stdin, &comm);
		io_channel_setup(&comm.io_stdin, fileno(stdin), (io_func_t) command_recv, cmd);
	}

DONE:
	if (ret != 0) {
#ifdef WITH_MQTT
		mqtt_shutdown(&comm.mqtt);
#endif
		hkcp_shutdown(&comm.hkcp);
		hk_advertise_shutdown(&comm.adv);
		hk_endpoints_shutdown(&comm.eps);
	}

	return ret;
}


int comm_tile_register(char *path)
{
	log_debug(2, "comm_tile_register '%s'", path);

	/* Create tile */
	hk_tile_t *tile = hk_tile_create(path);
	if (tile == NULL) {
		return -1;
	}

	/* Load tile */
        if (hk_tile_load(tile) < 0) {
		hk_tile_destroy(tile);
		return -1;
        }

	/* Start tile */
	hk_tile_start(tile);

	/* Add this tile to document root directory stack */
	char *ui = hk_tile_path(tile, "ui");
	if (ui != NULL) {
		if (is_dir(ui)) {
			ws_add_document_root(comm.ws, ui);
		}

		free(ui);
	}

	return 0;
}


int comm_sink_register(hk_obj_t *obj, int local, hk_ep_func_t func, void *user_data)
{
	hk_sink_t *sink = hk_sink_register(&comm.eps, obj, local);

	if (sink != NULL) {
		hk_sink_add_handler(sink, func, user_data);
		hk_sink_add_handler(sink, (hk_ep_func_t) comm_ws_send, comm.ws);
		if (comm.use_hkcp && (!local)) {
			/* Trigger advertising */
			hk_advertise_hkcp(&comm.adv);
		}
	}

	return hk_sink_id(sink);
}


void comm_sink_set_widget(int id, char *widget_name)
{
        hk_sink_t *sink = hk_sink_retrieve_by_id(&comm.eps, id);

        if (sink != NULL) {
                hk_sink_set_widget(sink, widget_name);
        }
        else {
		log_str("PANIC: Attempting to set widget on unknown sink #%d\n", id);
        }
}


void comm_sink_update_str(int id, char *value)
{
        hk_sink_t *sink = hk_sink_retrieve_by_id(&comm.eps, id);

        if (sink != NULL) {
                char *name = hk_sink_update(sink, value);

                if (name != NULL) {
                        comm_ws_send(comm.ws, &sink->ep);
                }
        }
        else {
		log_str("PANIC: Attempting to update unknown sink #%d\n", id);
        }
}


void comm_sink_update_int(int id, int value)
{
	char str[32];
	snprintf(str, sizeof(str), "%d", value);
	comm_sink_update_str(id, str);
}


int comm_source_register(hk_obj_t *obj, int local, int event)
{
	hk_source_t *source = hk_source_register(&comm.eps, obj, local, event);

	if (source != NULL) {
		if (comm.use_hkcp && (!local)) {
			/* Trigger advertising */
			hk_advertise_hkcp(&comm.adv);
		}
	}

	return hk_source_id(source);
}


void comm_source_set_widget(int id, char *widget_name)
{
        hk_source_t *source = hk_source_retrieve_by_id(&comm.eps, id);

        if (source != NULL) {
                hk_source_set_widget(source, widget_name);
        }
        else {
		log_str("PANIC: Attempting to set widget on unknown source #%d\n", id);
        }
}


void comm_source_update_str(int id, char *value)
{
	hk_source_t *source = hk_source_retrieve_by_id(&comm.eps, id);

	if (source == NULL) {
		log_str("PANIC: Attempting to update unknown source #%d\n", id);
		return;
	}

	char *name = hk_source_update(source, value);

	if (hk_source_is_public(source)) { 
		hkcp_source_update(&comm.hkcp, source, value);

#ifdef WITH_MQTT
		mqtt_publish(&comm.mqtt, name, value, hk_ep_flag_retain(&source->ep));
#endif
	}

	comm_ws_send(comm.ws, &source->ep);
}


void comm_source_update_int(int id, int value)
{
	char str[32];
	snprintf(str, sizeof(str), "%d", value);
	comm_source_update_str(id, str);
}


int comm_wget(char *uri, comm_recv_func_t *func, void *user_data)
{
	return ws_client_get(&comm.ws->client, uri, NULL, func, user_data);
}
