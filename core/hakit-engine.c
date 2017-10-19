/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "options.h"
#include "env.h"
#include "log.h"
#include "sys.h"
#include "mqtt.h"
#include "comm.h"
#include "mod.h"
#include "mod_init.h"
#include "ws_auth.h"

#include "lws_config.h"
#include "hakit_version.h"


#define PID_FILE "/var/run/hakit.pid"


//===================================================
// Command line arguments
//===================================================

const char *options_summary = "HAKit engine " HAKIT_VERSION " (" ARCH ")";

static char *opt_class_path = NULL;
static int opt_no_hkcp = 0;
static int opt_no_ssl = 0;
static int opt_insecure_ssl = 0;
static char *opt_http_auth = NULL;
static char *opt_cafile = NULL;
static int opt_no_mqtt = 0;
static char *opt_mqtt_broker = NULL;

static const options_entry_t options_entries[] = {
	{ "debug",   'd', 0, OPTIONS_TYPE_INT,    &opt_debug,   "Set debug level", "N" },
	{ "no-hkcp", 'n', 0, OPTIONS_TYPE_NONE,   &opt_no_hkcp, "Disable HKCP protocol" },
	{ "class-path", 'C', 0, OPTIONS_TYPE_STRING, &opt_class_path, "Comma-separated list of class directory pathes", "DIRS" },
#ifdef WITH_SSL
	{ "no-ssl",  's', 0, OPTIONS_TYPE_NONE,   &opt_no_ssl,  "Disable TLS/SSL - Do not use encryption/authentication for HKCP/MQTT/HTTP exchanges" },
	{ "insecure", 'k', 0, OPTIONS_TYPE_NONE,   &opt_insecure_ssl,  "Allow insecure HTTP TLS/SSL for client connections (self-signed certificates)" },
#endif
	{ "http-auth", 'A', 0, OPTIONS_TYPE_STRING, &opt_http_auth, "HTTP Authentication file. Authentication is disabled if none is specified", "FILE" },
	{ "cafile",    'C', 0, OPTIONS_TYPE_STRING, &opt_cafile,    "Certificate Authority file for TLS/SSL authentication", "FILE" },
#ifdef WITH_MQTT
	{ "no-mqtt",        'm', 0, OPTIONS_TYPE_NONE,   &opt_no_mqtt, "Disable MQTT protocol" },
	{ "mqtt-broker",    'b', 0, OPTIONS_TYPE_STRING, &opt_mqtt_broker, "MQTT broker specification", "[USER[:PASSWORD]@]HOST[:PORT]" },
#endif
	{ NULL }
};


//===================================================
// Program body
//===================================================

int main(int argc, char *argv[])
{
	int use_ssl = 1;  // 0 = disable SSL, 2 = allow insecure SSL
	int i;

	if (options_parse(options_entries, &argc, argv) != 0) {
		exit(1);
	}

	/* Init exec environment */
	env_init(argc, argv);

	/* Init log management */
	log_init("hakit");
	log_str(options_summary);
	log_str("Using libwebsockets version " LWS_LIBRARY_VERSION " build " LWS_BUILD_HASH);

        /* Enable per-line output buffering */
        setlinebuf(stdout);

	/* Init system runtime */
	sys_init();

	/* Init communication engine */
	if (opt_no_ssl) {
		use_ssl = 0;
	}
	else if (opt_insecure_ssl) {
		use_ssl = 2;
	}

	if (comm_init(use_ssl, opt_cafile,
                      opt_no_hkcp ? 0:1,
                      opt_no_mqtt ? 0:1, opt_mqtt_broker)) {
		return 2;
	}

	if (opt_http_auth != NULL) {
		ws_auth_init(opt_http_auth);
	}

	/* Init module management */
	if (hk_mod_init(opt_class_path)) {
		return 2;
	}

        /* Load application */
	for (i = 1; i < argc; i++) {
		char *path = argv[i];
		if (comm_tile_register(path) < 0) {
			return 3;
		}
	}

	sys_run();

	return 0;
}
