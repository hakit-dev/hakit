/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014-2017 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/utsname.h>

#include "options.h"
#include "log.h"
#include "sys.h"
#include "ws_utils.h"
#include "ws_client.h"

#include "hakit_version.h"


#define NAME "hakit-launcher"
#define PLATFORM_URL "https://hakit.net/api/"


//===================================================
// Command line arguments
//==================================================

const char *options_summary = "HAKit launcher " HAKIT_VERSION " (" ARCH ")";


static const options_entry_t options_entries[] = {
	{ "debug",  'd', 0,    OPTIONS_TYPE_INT,  &opt_debug,   "Set debug level", "N" },
	{ "daemon", 'D', 0,    OPTIONS_TYPE_NONE, &opt_daemon,  "Run in background as a daemon" },
	{ NULL }
};


//===================================================
// Platform access
//==================================================

static char *opt_api_key = NULL;

static const options_entry_t api_auth_entries[] = {
	{ "api-key", 'K', 0,   OPTIONS_TYPE_STRING,   &opt_api_key,  "API key for accessing hakit.net web platform" },
};

static char *platform_http_header(void)
{
	static char *header = NULL;

	if (header == NULL) {
		struct utsname u;
		int size = 0;
		int len = 0;

		// HAKit version
		size += strlen(HAKIT_VERSION) + 20;
		header = realloc(header, size);
		len = snprintf(header+len, size-len, "HAKit-Version: " HAKIT_VERSION "\r\n");

		// Get system information
		if (uname(&u) == 0) {
			size += strlen(u.sysname) + strlen(u.nodename) + strlen(u.release) + strlen(u.version) + strlen(u.machine) + 36;
			header = realloc(header, size);
			len += snprintf(header+len, size-len, "HAKit-OS: %s %s %s %s\r\n", u.sysname, u.release, u.version, u.machine);
			len += snprintf(header+len, size-len, "HAKit-Hostname: %s\r\n", u.nodename);
		}
		else {
			log_str("WARNING: Cannot retrieve system identification: %s", strerror(errno));
		}

		// API key
		if (opt_api_key != NULL) {
			size += strlen(opt_api_key) + 20;
			header = realloc(header, size);
			len += snprintf(header+len, size-len, "HAKit-Api-Key: %s\r\n", opt_api_key);
		}
	}

	return header;
}


//===================================================
// Device id
//==================================================

static ws_client_t ws_client;


static void device_advertise_response(void *user_data, char *buf, int len)
{
	log_debug(3, "device_advertise_response: len=%d", len);
}


static void device_advertise_request(void)
{
	ws_client_get(&ws_client, PLATFORM_URL "hello.php", platform_http_header(), device_advertise_response, NULL);
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

	log_str("Starting in daemon mode: pid=%d", getpid());
}


int main(int argc, char *argv[])
{
	if (options_parse(options_entries, &argc, argv) != 0) {
		exit(1);
	}

	if (opt_daemon) {
		run_as_daemon();
	}

	/* Init log management */
	log_init(NAME);
	log_str(options_summary);

	options_conf_parse(api_auth_entries, "platform");

	/* Init system runtime */
	sys_init();

	// Setup LWS logging
	ws_log_init(opt_debug);

	// Setup WS HTTP client
	memset(&ws_client, 0, sizeof(ws_client));
	if (ws_client_init(&ws_client, 1) < 0) {
		log_str("ERROR: Failed to init HTTP client");
		return 1;
	}

	/* Advertise device */
	device_advertise_request();

	sys_run();

	return 0;
}
