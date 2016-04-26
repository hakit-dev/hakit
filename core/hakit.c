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
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>

#include "types.h"
#include "options.h"
#include "env.h"
#include "log.h"
#include "sys.h"
#include "comm.h"
#include "mod.h"
#include "mod_init.h"
#include "mod_load.h"

#include "lws_config.h"
#include "hakit_version.h"


//===================================================
// Command line arguments
//===================================================

const char *options_summary = "HAKit " HAKIT_VERSION " (" ARCH ")";
static int opt_monitor = 0;
static char *opt_class_path = NULL;
static char *opt_hosts = NULL;
static int opt_no_hkcp = 0;
static int opt_no_ssl = 0;

const options_entry_t options_entries[] = {
	{ "debug",   'd', 0, OPTIONS_TYPE_INT,    &opt_debug,   "Set debug level", "N" },
	{ "daemon",  'D', 0, OPTIONS_TYPE_NONE,   &opt_daemon,  "Run in background as a daemon" },
	{ "no-hkcp", 'n', 0, OPTIONS_TYPE_NONE,   &opt_no_hkcp, "Disable HKCP protocol" },
	{ "hosts",   'H', 0, OPTIONS_TYPE_STRING, &opt_hosts,   "Comma-separated list of explicit HKCP host names", "HOST" },
	{ "monitor", 'm', 0, OPTIONS_TYPE_NONE,   &opt_monitor, "Enable HKCP monitor mode" },
	{ "class-path", 'C', 0, OPTIONS_TYPE_STRING, &opt_class_path, "Comma-separated list of class directory pathes", "DIRS" },
#ifdef WITH_SSL
	{ "no-ssl",  's', 0, OPTIONS_TYPE_NONE,   &opt_no_ssl,  "Disable SSL - Access status/dashboard using HTTP instead of HTTPS" },
#endif
	{ NULL }
};


//===================================================
// Monitor mode management
//===================================================

static void monitor_sink_event(void *user_data, char *name, char *value)
{
	if (value == NULL) {
		log_str("-- New sink: %s", name);
	}
	else {
		log_str("-- %s='%s'", name, value);
	}
}


//===================================================
// Program body
//===================================================

static void run_as_daemon(void)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		log_str("ERROR: Fork failed: %s", strerror(errno));
		exit(3);
	}

	if (pid > 0) {
		exit(0);
	}

	if (setsid() < 0) {
		log_str("ERROR: Setsid failed: %s", strerror(errno));
		exit(3);
 	}

	close(STDIN_FILENO);
}


int main(int argc, char *argv[])
{
	char *app;

	if (options_parse(&argc, argv) != 0) {
		exit(1);
	}

	if (opt_daemon) {
		run_as_daemon();
	}

	/* Init exec environment */
	env_init(argc, argv);

	/* Init log management */
	log_init("hakit");
	log_str(options_summary);
	log_str("Using libwebsockets version " LWS_LIBRARY_VERSION " build " LWS_BUILD_HASH);

	/* Init system runtime */
	sys_init();

	/* Init communication engine */
	if (comm_init(opt_no_ssl ? 0:1, opt_no_hkcp ? 0:1, opt_hosts)) {
		return 2;
	}

	if (opt_monitor) {
		comm_monitor((comm_sink_func_t) monitor_sink_event, NULL);
	}

	/* Init module management */
	if (hk_mod_init(opt_class_path)) {
		return 2;
	}

	app = env_app();
	if (app != NULL) {
		if (hk_mod_load(app)) {
			return 3;
		}
		hk_obj_start_all();
	}

	sys_run();

	return 0;
}
