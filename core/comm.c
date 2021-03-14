/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2021 Sylvain Giroudon
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
#include "tstamp.h"
#include "log.h"
#include "io.h"
#include "ws.h"
#include "ws_events.h"
#include "ws_client.h"
#include "hkcp.h"
#include "hkcp_cmd.h"
#include "mqtt.h"
#include "command.h"
#include "endpoint.h"
#include "advertise.h"
#include "mod_load.h"
#include "mod.h"
#include "trace.h"
#include "comm.h"


#define HAKIT_HKCP_PORT 5678   // Default HAKit HKCP communication port
#define HAKIT_HTTP_PORT 5680   // Default HAKit HTTP port

#define HAKIT_SHARE_DIR "/usr/share/hakit/"

typedef struct {
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
	int size = strlen(tile_name) + strlen(name) + strlen(value) + 24;
        unsigned long long t = tstamp_ms();
	char str[size];

	/* Send WebSocket event */
        if (hk_tile_nmemb() > 1) {
                snprintf(str, size, "!%llu,%s.%s=%s", t, tile_name, name, value);
        }
        else {
                snprintf(str, size, "!%llu,%s=%s", t, name, value);
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


static void comm_mqtt_connected(mqtt_t *mqtt)
{
	hk_source_foreach_public((hk_ep_func_t) comm_mqtt_publish, mqtt);
	hk_sink_foreach_public((hk_ep_func_t) comm_mqtt_subscribe, mqtt);
}


static void comm_mqtt_update(mqtt_t *mqtt, char *name, char *value)
{
	if (name != NULL) {
		log_debug(2, "comm_mqtt_update %s='%s'", name, value);
		hk_sink_update_by_name(name, value);
	}
	else {
		log_debug(2, "comm_mqtt_update CONNECTED");
		comm_mqtt_connected(mqtt);
	}
}

static void comm_mqtt_discover(mqtt_t *mqtt, char *remote_ip, char *broker)
{
	log_debug(2, "comm_mqtt_discover %s '%s'", remote_ip, broker);
	if (broker != NULL) {
		mqtt_connect(mqtt, broker);
	}
}

#endif /* WITH_MQTT */


typedef struct {
        buf_t *out_buf;
        uint64_t t1;
        uint64_t t2;
} comm_command_ctx_t;

static int comm_command_trace_dump(comm_command_ctx_t *ctx, hk_ep_t *ep)
{
        hk_trace_dump(&ep->tr, ctx->t1, ctx->t2, ctx->out_buf);
        return 1;
}


static int comm_command_trace(int argc, char **argv, buf_t *out_buf)
{
        char *name = NULL;
        uint64_t t1 = 0;
        uint64_t t2 = 0;
        int i;

        if (argc > 3) {
                goto USAGE;
        }

        for (i = 1; i < argc; i++) {
                char *args = argv[i];
                char *sep = strchr(args, ':');
                if (sep != NULL) {
                        *(sep++) = '\0';
                        if (*args != '\0') {
                                t1 = strtoull(args, NULL, 0);
                        }
                        if (*sep != '\0') {
                                t2 = strtoull(sep, NULL, 0);
                        }
                }
                else {
                        if (name != NULL) {
                                goto USAGE;
                        }
                        name = args;
                }
        }

        if (name != NULL) {
                hk_ep_t *ep = HK_EP(hk_source_retrieve_by_name(name));
                if (ep == NULL) {
                        ep = HK_EP(hk_sink_retrieve_by_name(name));
                        if (ep == NULL) {
                                log_str("ERROR: Unknown endpoint '%s'", name);
                                return -1;
                        }
                }

                hk_trace_dump(&ep->tr, t1, t2, out_buf);
        }
        else {
                comm_command_ctx_t ctx = {
                        .out_buf = out_buf,
                        .t1 = t1,
                        .t2 = t2,
                };

                hk_source_foreach((hk_ep_foreach_func_t) comm_command_trace_dump, &ctx);
                hk_sink_foreach((hk_ep_foreach_func_t) comm_command_trace_dump, &ctx);
        }

	buf_append_str(out_buf, ".\n");

        return 0;

USAGE:
        log_str("ERROR: Usage: %s [<endpoint>] [[<t1>]:[<t2>]]", argv[0]);
        return -1;
}


static void comm_command_tiles_dump(buf_t *out_buf, hk_tile_t *tile)
{
	buf_append_str(out_buf, tile->name);
	buf_append_str(out_buf, "\n");
}


static int comm_command_tiles(int argc, char **argv, buf_t *out_buf)
{
        hk_tile_foreach((hk_tile_foreach_func) comm_command_tiles_dump, out_buf);
	buf_append_str(out_buf, ".\n");
        return 0;
}


static void comm_command_ws(hkcp_t *hkcp, int argc, char **argv, buf_t *out_buf)
{
        if (strcmp(argv[0], "trace") == 0) {
                comm_command_trace(argc, argv, out_buf);
        }
        else if (strcmp(argv[0], "tiles") == 0) {
                comm_command_tiles(argc, argv, out_buf);
        }
        else {
                hkcp_command(hkcp, argc, argv, out_buf);
        }
}


static void comm_command_stdin(hkcp_t *hkcp, int argc, char **argv)
{
	buf_t out_buf;

	if (argc > 0) {
		buf_init(&out_buf);

                comm_command_ws(hkcp, argc, argv, &out_buf);

                if (out_buf.len > 0) {
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


int comm_init(int use_ssl, char *certs, int use_hkcp, int advertise)
{
	char *path = NULL;
	int ret = 0;

	memset(&comm, 0, sizeof(comm));

	/* Init endpoint management */
	hk_endpoints_init();

	/* Init advertising protocol */
        if (hk_advertise_init(&comm.adv, advertise ? HAKIT_HKCP_PORT:0)) {
                ret = -1;
                goto DONE;
        }

	/* Init HKCP gears */
	comm.use_hkcp = use_hkcp;
	ret = hkcp_init(&comm.hkcp, use_hkcp ? HAKIT_HKCP_PORT:0, certs);
	if (ret != 0) {
		goto DONE;
	}

	if (use_hkcp) {
		/* Handle HKCP advertisement */
		hk_advertise_handler(&comm.adv, ADVERTISE_PROTO_HKCP, (hk_advertise_func_t) hkcp_node_add, &comm.hkcp);
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
	comm.ws = ws_new(HAKIT_HTTP_PORT, use_ssl, path);
        if (path != NULL) {
                free(path);
        }
	if (comm.ws == NULL) {
		ret = -1;
		goto DONE;
	}

	/* Setup document root directory stack */
	path = env_devdir(NULL);
	if (path != NULL) {
		log_debug(2, "Running from development environment: %s", path);
                ws_add_document_root(comm.ws, path);
                free(path);
	}
	else {
                ws_add_document_root(comm.ws, HAKIT_SHARE_DIR);
	}


	ws_set_command_handler(comm.ws, (ws_command_handler_t) comm_command_ws, &comm.hkcp);

	/* Setup stdin command handler if not running as a daemon */
	if (!opt_daemon) {
		command_t *cmd = command_new((command_handler_t) comm_command_stdin, &comm.hkcp);
		io_channel_setup(&comm.io_stdin, fileno(stdin), (io_func_t) command_recv, cmd);
	}

DONE:
	if (ret != 0) {
		hkcp_shutdown(&comm.hkcp);
		hk_advertise_shutdown(&comm.adv);
		hk_endpoints_shutdown();
	}

	return ret;
}


int comm_enable_mqtt(char *certs, char *mqtt_broker)
{
#ifdef WITH_MQTT
        /* Init MQTT gears */
        if (mqtt_init(&comm.mqtt, certs, (mqtt_update_func_t) comm_mqtt_update, &comm.mqtt)) {
                return -1;
        }

        if (mqtt_broker != NULL) {
                /* Connect to broker */
                if (mqtt_connect(&comm.mqtt, mqtt_broker)) {
                        mqtt_shutdown(&comm.mqtt);
                        return -1;
                }
        }
        else {
                /* Handle MQTT advertisement */
                hk_advertise_handler(&comm.adv, ADVERTISE_PROTO_MQTT, (hk_advertise_func_t) comm_mqtt_discover, &comm.mqtt);
        }

        /* Advertise MQTT protocol */
        hk_advertise_mqtt(&comm.adv, mqtt_broker);

        return 0;
#else /* WITH_MQTT */
        log_str("ERROR: MQTT not available in this release");
        return -1;
#endif /* !WITH_MQTT */
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
	char *rootdir = hk_tile_rootdir(tile);
        ws_add_document_root(comm.ws, rootdir);
        free(rootdir);

	return 0;
}


hk_sink_t *comm_sink_register(hk_obj_t *obj, int local, hk_ep_func_t func, void *user_data)
{
	hk_sink_t *sink = hk_sink_register(obj, local);

	if (sink != NULL) {
		hk_sink_add_handler(sink, func, user_data);
		hk_sink_add_handler(sink, (hk_ep_func_t) comm_ws_send, comm.ws);
		if (comm.use_hkcp && (!local)) {
			/* Trigger advertising */
			hk_advertise_hkcp(&comm.adv);
		}
	}

	return sink;
}


void comm_sink_update_str(hk_sink_t *sink, char *value)
{
        /* Update endpoint */
        hk_sink_update(sink, value);

        /* Update websocket link */
        comm_ws_send(comm.ws, HK_EP(sink));
}


hk_source_t *comm_source_register(hk_obj_t *obj, int local, int event)
{
	hk_source_t *source = hk_source_register(obj, local, event);

	if (source != NULL) {
		if (comm.use_hkcp && (!local)) {
			/* Trigger advertising */
			hk_advertise_hkcp(&comm.adv);
		}
	}

	return source;
}


void comm_source_update_str(hk_source_t *source, char *value)
{
        /* Update endpoint */
	hk_source_update(source, value);

        /* Update networked links */
	if (hk_source_is_public(source)) { 
		hkcp_source_update(&comm.hkcp, source);

#ifdef WITH_MQTT
                comm_mqtt_publish(&comm.mqtt, HK_EP(source));
#endif
	}

        /* Update websocket link */
	comm_ws_send(comm.ws, HK_EP(source));
}
