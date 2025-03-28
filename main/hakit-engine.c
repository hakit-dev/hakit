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

#include "hakit_version.h"


#define PID_FILE "/var/run/hakit.pid"


//===================================================
// Command line arguments
//===================================================

const char *options_summary = "HAKit engine " HAKIT_VERSION " (" ARCH ")";

static char *opt_class_path = NULL;
static int opt_no_advertise = 0;
static int opt_no_hkcp = 0;
static int opt_no_https = 0;
static int opt_insecure_ssl = 0;
static char *opt_http_auth = NULL;
static char *opt_http_alias = NULL;
static char *opt_certs = NULL;
static int opt_no_mqtt = 0;
static char *opt_mqtt_broker = NULL;
static int opt_trace_depth = 0;
extern int opt_full_name;

static const options_entry_t options_entries[] = {
	{ "debug",        'd', OPTION_FLAG_NONE, OPTIONS_TYPE_INT,    &opt_debug,        "Set debug level", "N" },
	{ "no-advertise", 'N', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_no_advertise, "Disable HKCP/MQTT advertising" },
	{ "no-hkcp",      'n', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_no_hkcp,      "Disable HKCP protocol" },
	{ "class-path",   'C', OPTION_FLAG_LIST, OPTIONS_TYPE_STRING, &opt_class_path,   "Comma-separated list of class directory pathes", "DIRS" },
	{ "trace-depth",  't', OPTION_FLAG_NONE, OPTIONS_TYPE_INT,    &opt_trace_depth,  "Set trace recording depth for user interface charts.", "DEPTH" },
	{ "full-name",    'f', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_full_name,    "Use fully qualified endpoint names. Do not connect local sinks/sources together." },
#ifdef WITH_SSL
	{ "no-https",     's', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_no_https,     "Use HTTP instead of HTTPS" },
	{ "insecure",     'k', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_insecure_ssl, "Allow insecure HTTP TLS/SSL for client connections (self-signed certificates)" },
	{ "certs",        'e', OPTION_FLAG_NONE, OPTIONS_TYPE_STRING, &opt_certs,        "TLS/SSL certificate directory for HKCP/MQTT exchanges", "DIR" },
#endif
	{ "http-auth",    'A', OPTION_FLAG_NONE, OPTIONS_TYPE_STRING, &opt_http_auth,    "HTTP Authentication file. Authentication is disabled if none is specified", "FILE" },
	{ "http-alias",   'a', OPTION_FLAG_LIST, OPTIONS_TYPE_STRING, &opt_http_alias,   "Set HTTP alias to directory", "ALIAS=DIR,..." },
#ifdef WITH_MQTT
	{ "no-mqtt",      'm', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_no_mqtt,      "Disable MQTT protocol" },
	{ "mqtt-broker",  'b', OPTION_FLAG_NONE, OPTIONS_TYPE_STRING, &opt_mqtt_broker,  "MQTT broker specification", "[USER[:PASSWORD]@]HOST[:PORT]" },
#endif
	{ NULL }
};


//===================================================
// Program body
//===================================================

extern const hk_class_t *static_classes[];


int main(int argc, char *argv[])
{
	int use_ssl = 1;  // 0 = disable SSL, 2 = allow insecure SSL
	int i;

	if (options_parse(options_entries, "engine*", &argc, argv) != 0) {
		exit(1);
	}

	/* Init exec environment */
	env_init(argc, argv);

	/* Init log management */
	log_init("hakit");
	log_str(options_summary);

        /* Enable per-line output buffering */
        setlinebuf(stdout);

	/* Init system runtime */
	sys_init();

	/* Init communication engine */
	if (opt_no_https) {
		use_ssl = 0;
	}
	else if (opt_insecure_ssl) {
		use_ssl = 2;
	}

	if (comm_init(use_ssl, opt_certs,
                      opt_no_hkcp ? 0:1,
                      opt_no_advertise ? 0:1)) {
		return 2;
	}

        if (!opt_no_mqtt) {
                if (comm_enable_mqtt(opt_certs, opt_mqtt_broker)) {
                        return 2;
                }
        }

        hk_endpoints_set_trace_depth(opt_trace_depth);

	if (opt_http_auth != NULL) {
		ws_auth_init(opt_http_auth);
	}

        /* Register HTTP URL aliases */
        char *alias = opt_http_alias;
        while (alias != NULL) {
                char *sep = strchr(alias, ',');
                if (sep != NULL) {
                        *(sep++) = '\0';
                }
                char *eq = strchr(alias, '=');
                if (eq != NULL) {
                        *(eq++) = '\0';
                        comm_alias_register(alias, eq);
                }
                alias = sep;
        }

        /* Init static class modules */
        for (i = 0; static_classes[i] != NULL; i++) {
                hk_class_register((hk_class_t *) static_classes[i]);
        }
        
	/* Init loadable class module management */
	hk_mod_init(opt_class_path);

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
