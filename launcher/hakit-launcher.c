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
#include <sys/stat.h>

#include "options.h"
#include "log.h"
#include "sys.h"
#include "buf.h"
#include "tab.h"
#include "ws_client.h"

#include "hakit_version.h"


#define NAME "hakit-launcher"
#define PLATFORM_URL "https://hakit.net/api/"
//#define PLATFORM_URL "http://localhost/api/"

#define LIB_DIR "/var/lib/hakit"
#define APPS_DIR LIB_DIR "/apps"

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

#define VERSION HAKIT_VERSION
const char *options_summary = "HAKit launcher " VERSION " (" ARCH ")";

static const char *opt_lib_dir = LIB_DIR;

static const options_entry_t options_entries[] = {
	{ "debug",  'd', 0,    OPTIONS_TYPE_INT,  &opt_debug,   "Set debug level", "N" },
	{ "daemon", 'D', 0,    OPTIONS_TYPE_NONE, &opt_daemon,  "Run in background as a daemon" },
	{ "lib-dir", 'L', 0,   OPTIONS_TYPE_STRING,  &opt_lib_dir,  "Lib directory to store application and config files", "DIR" },
	{ NULL }
};


//===================================================
// Environment
//===================================================

static char *env_apps_dir = NULL;


static int env_mkdir(const char *dir)
{
        if (mkdir(dir, 0755) == 0) {
                log_str("INFO: Directory '%s' created", dir);
        }
        else {
                if (errno != EEXIST) {
                        log_str("ERROR: Cannot create directory '%s': %s", dir, strerror(errno));
                        return -1;
                }
        }

        return 0;
}

static int env_init(void)
{
        // Create lib directory
        if (env_mkdir(opt_lib_dir) != 0) {
                return -1;
        }

        return 0;
}


static char *env_app_dir(char *app_name)
{
        int size;
        char *dir;

        // Create apps directory
        if (env_apps_dir == NULL) {
                size = strlen(opt_lib_dir) + 8;
                env_apps_dir = malloc(size);
                snprintf(env_apps_dir, size, "%s/apps", opt_lib_dir);

                if (env_mkdir(env_apps_dir) != 0) {
                        free(env_apps_dir);
                        env_apps_dir = NULL;
                        return NULL;
                }
        }

        size = strlen(env_apps_dir) + strlen(app_name) + 2;
        dir = malloc(size);
        snprintf(dir, size, "%s/%s", env_apps_dir, app_name);

        if (env_mkdir(dir) != 0) {
                free(dir);
                return NULL;
        }

        return dir;
}


//===================================================
// Platform access
//==================================================

static char *opt_api_key = NULL;

static const options_entry_t api_auth_entries[] = {
	{ "api-key", 'K', 0,   OPTIONS_TYPE_STRING,   &opt_api_key,  "API key for accessing hakit.net web platform" },
};

static void platform_http_header(buf_t *header)
{
        // API key
        if (opt_api_key != NULL) {
                buf_append_fmt(header, "HAKit-Api-Key: %s\r\n", opt_api_key);
        }
}


static char **platform_get_lines(char *buf, int len)
{
	int argc = 0;
	char **argv = malloc(sizeof(char *));
	int i = 0;

        log_debug(2, "platform_get_lines len=%d", len);

	while (i < len) {
		char *s = &buf[i];

		// Find and cut trailing CR/LF
		while ((i < len) && (buf[i] >= ' ')) {
			i++;
		}
		while ((i < len) && (buf[i] < ' ')) {
			buf[i++] = '\0';
		}

		// Skip leading blanks
		while ((*s != '\0') && (*s <= ' ')) {
			s++;
		}

                log_debug(2, "  [%d]='%s'", argc, s);

		argv = realloc(argv, sizeof(char *) * (argc+2));
		argv[argc++] = s;
	}

	argv[argc] = NULL;

	return argv;
}


static int platform_get_status(char *buf, int len, char **errstr, int *pofs)
{
	int errcode = -1;
        int i = 0;

        while ((i < len) && (buf[i] >= ' ')) {
                i++;
        }
        while ((i < len) && (buf[i] != '\0') && (buf[i] < ' ')) {
                buf[i++] = '\0';
        }
        if (pofs != NULL) {
                *pofs = i;
        }

        char *s = strchr(buf, ' ');
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


static char *platform_get_prop(char **argv, char *key)
{
	char *value = NULL;
	int i;

	for (i = 0; (argv[i] != NULL) && (value == NULL); i++) {
		char *args = argv[i];
		char *s = strchr(args, ':');
		if (s != NULL) {
			char c = *s;
			*s = '\0';
			if (strcmp(args, key) == 0) {
				value = s+1;
				while ((*value != '\0') && (*value <= ' ')) {
					value++;
				}
			}
			*s = c;
		}
	}

	return value;
}


//===================================================
// APT configuration
//==================================================

static int apt_config_request(void)
{
	return 0;
}


//===================================================
// Application
//==================================================

typedef struct {
        int argc;
	char **argv;
	int index;
} app_ctx_t;


typedef struct {
        char *fname;
} app_t;

static HK_TAB_DECLARE(apps, app_t);
#define APP_ENTRY(i) HK_TAB_PTR(apps, app_t, i)


static void hello_retry(void);
static void app_request_send(app_ctx_t *ctx);


static app_ctx_t *app_ctx_alloc(void)
{
	app_ctx_t *ctx = malloc(sizeof(app_ctx_t));
        ctx->argc = 0;
	ctx->argv = malloc(sizeof(char *));
	ctx->index = 0;
        ctx->argv[0] = NULL;

        return ctx;
}


static void app_ctx_feed(app_ctx_t *ctx, char *str)
{
        ctx->argv = realloc(ctx->argv, sizeof(char *) * (ctx->argc+2));
        ctx->argv[(ctx->argc)++] = strdup(str);
        ctx->argv[ctx->argc] = NULL;
}


static void app_ctx_cleanup(app_ctx_t *ctx)
{
        int i;

        for (i = 0; i < ctx->argc; i++) {
                free(ctx->argv[i]);
                ctx->argv[i] = NULL;
        }

        free(ctx->argv);
        ctx->argv = NULL;

        free(ctx);
}


static void app_response(app_ctx_t *ctx, char *buf, int len)
{
	log_debug(2, "app_response: index=%d len=%d", ctx->index, len);

        // Get error code
        char *errstr = NULL;
        int ofs = 0;
        int errcode = platform_get_status(buf, len, &errstr, &ofs);

        if (errcode == 0) {
                // Store application data
                char *dir = env_app_dir(ctx->argv[ctx->index]);

                if (dir != NULL) {
                        app_t *app = hk_tab_push(&apps);
                        int size = strlen(dir)+8;
                        FILE *f;

                        app->fname = malloc(size);
                        snprintf(app->fname, size, "%s/app.hk", dir);

                        f = fopen(app->fname, "w");
                        if (f != NULL) {
                                char *buf1 = buf + ofs;
                                int len1 = len - ofs;
                                int wlen = fwrite(buf1, 1, len1, f);
                                if (wlen != len1) {
                                        log_str("ERROR: Failed to write file '%s': %s", app->fname, strerror(errno));
                                        errcode = -1;
                                }
                                fclose(f);
                        }
                        else {
                                log_str("ERROR: Failed to create file '%s': %s", app->fname, strerror(errno));
                                errcode = -1;
                        }

                        free(dir);
                }
        }

        if (errcode == 0) {
                // Ask for next application
                ctx->index++;
                app_request_send(ctx);
        }
        else {
                hello_retry();
        }
}


static void app_request_send(app_ctx_t *ctx)
{
	char *str = ctx->argv[ctx->index];
        buf_t header;

	log_debug(2, "app_request_send [%d]='%s'", ctx->index, str);

	if (str == NULL) {
                app_ctx_cleanup(ctx);
		//TODO: Next step
		return;
	}

	// API key
        buf_init(&header);
	platform_http_header(&header);

	// Application name
        buf_append_fmt(&header, "HAKit-Application: %s\r\n", str);

	ws_client_get(&ws_client, PLATFORM_URL "app.php", (char *) header.base, (ws_client_func_t *) app_response, ctx);

        buf_cleanup(&header);
}


static int app_request(char *app_list)
{
	app_ctx_t *ctx;

	log_debug(2, "app_request app_list='%s'", app_list);

        hk_tab_cleanup(&apps);
	ctx = app_ctx_alloc();

	while ((app_list != NULL) && (*app_list != '\0')) {
		char *s = strchr(app_list, ' ');
		if (s != NULL) {
			*s = '\0';
		}

		while ((*app_list != '\0') && (*app_list <= ' ')) {
			app_list++;
		}
		if (*app_list != '\0') {
                        app_ctx_feed(ctx, app_list);
		}

		if (s != NULL) {
			*s = ' ';
			while ((*s != '\0') && (*s <= ' ')) {
				s++;
			}
		}

		app_list = s;
	}

	app_request_send(ctx);

	return 0;
} 


//===================================================
// Device identification
//==================================================

static void hello_response(void *user_data, char *buf, int len)
{
	log_debug(2, "hello_response: len=%d", len);

	if (len > 0) {
		char *errstr = NULL;
                int ofs = 0;
		int errcode = platform_get_status(buf, len, &errstr, &ofs);

		if (errcode >= 0) {
			if (errcode == 0) {
				log_str("INFO    : Device accepted by platform server");
                                char **argv = platform_get_lines(buf+ofs, len-ofs);
				char *app_list = platform_get_prop(argv, "Applications");
				if (app_list != NULL) {
					app_request(app_list);
				}
                                free(argv);
			}
			else {
				log_str("WARNING : Access denied by platform server: %s", errstr);
			}
		}
		else {
			log_str("ERROR   : Bad response from platform server");
		}

		if (errcode) {
                        hello_retry();
		}
	}
}


static int hello_request(void)
{
	struct utsname u;
        buf_t header;

	log_debug(2, "hello_request");
	platform_state = ST_HELLO;

	// API key
        buf_init(&header);
	platform_http_header(&header);
	
	// HAKit version
        buf_append_str(&header, "HAKit-Version: " VERSION "\r\n");

	// Get system information
	if (uname(&u) == 0) {
                buf_append_fmt(&header, "HAKit-OS: %s %s %s %s\r\n", u.sysname, u.release, u.version, u.machine);
                buf_append_fmt(&header, "HAKit-Hostname: %s\r\n", u.nodename);
	}
	else {
		log_str("WARNING: Cannot retrieve system identification: %s", strerror(errno));
	}

	ws_client_get(&ws_client, PLATFORM_URL "hello.php", (char *) header.base, hello_response, NULL);

        buf_cleanup(&header);

	return 0;
}

	
static void hello_retry(void)
{
        log_str("INFO    : New HELLO attempt in %d seconds", HELLO_RETRY_DELAY);
        sys_timeout(HELLO_RETRY_DELAY*1000, (sys_func_t) hello_request, NULL);
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

        if (env_init() != 0) {
		exit(2);
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
