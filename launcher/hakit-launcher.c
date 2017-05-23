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
#include "ws_client.h"

#include "version.h"


#define NAME "hakit-launcher"
#define PLATFORM_URL "https://hakit.net/api/"

#define HELLO_RETRY_DELAY (3*60)

typedef enum {
	ST_IDLE=0,
	ST_HELLO
} platform_state_t;

static platform_state_t platform_state = ST_HELLO;

static ws_client_t ws_client;


//===================================================
// Command line arguments
//==================================================

const char *options_summary = "HAKit launcher " VERSION " (" ARCH ")";


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

static char *platform_http_header(int *plen)
{
	static char *header = NULL;
	static int header_len = 0;

	if (header == NULL) {
		// API key
		if (opt_api_key != NULL) {
			int size = strlen(opt_api_key) + 20;
			header = malloc(size);
			header_len = snprintf(header, size, "HAKit-Api-Key: %s\r\n", opt_api_key);
		}
	}

	if (plen != NULL) {
		*plen = header_len;
	}

	return header;
}


static int platform_parse_response(char *buf, int len, char **errstr)
{
	int errcode = -1;
	char *s;

	// Strip trailing CR/LF
	while ((len > 0) && (buf[len-1] < ' ')) {
		len--;
	}

	buf[len] = '\0';

	s = strchr(buf, ' ');
	if (s != NULL) {
		*(s++) = '\0';

		if (errstr != NULL) {
			while ((*s != '\0') && (*s <= ' ')) {
				s++;
			}
			*errstr = s;
		}

		errcode = atoi(buf);
	}

	return errcode;
}


//===================================================
// APT configuration
//==================================================

static int apt_config_request(void)
{
	return 0;
}


//===================================================
// Device identification
//==================================================

static int hello_request(void);


static void hello_response(void *user_data, char *buf, int len)
{
	char *errstr = NULL;
	int errcode;

	log_debug(3, "hello_response: len=%d", len);

	errcode = platform_parse_response(buf, len, &errstr);
	if (errcode >= 0) {
		if (errcode == 0) {
			log_str("INFO    : Device accepted by platform server");
			apt_config_request();
		}
		else {
			log_str("WARNING : Access denied by platform server: %s", errstr);
		}
	}
	else {
		log_str("ERROR   : Bad response from platform server");
	}

	if (errcode) {
		log_str("INFO    : New HELLO attempt in %d seconds", HELLO_RETRY_DELAY);
		sys_timeout(HELLO_RETRY_DELAY*1000, (sys_func_t) hello_request, NULL);
	}
}


static int hello_request(void)
{
	struct utsname u;
	char *header;
	int size;
	int len;

	log_debug(3, "hello_request");
	platform_state = ST_HELLO;

	// API key
	header = platform_http_header(&len);
	
	// HAKit version
	size = len + strlen(VERSION) + 20;
	header = realloc(header, size);
	len += snprintf(header+len, size-len, "HAKit-Version: " VERSION "\r\n");

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

	ws_client_get(&ws_client, PLATFORM_URL "hello.php", header, hello_response, NULL);

	free(header);

	return 0;
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

	// Setup WS HTTP client
	memset(&ws_client, 0, sizeof(ws_client));
	if (ws_client_init(&ws_client, 1) < 0) {
		log_str("ERROR: Failed to init HTTP client");
		return 1;
	}

	/* Advertise device */
	hello_request();

	sys_run();

	return 0;
}
