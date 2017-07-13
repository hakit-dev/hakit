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
#include "env.h"
#include "buf.h"
#include "tab.h"
#include "ws_client.h"
#include "proc.h"

#include "hakit_version.h"


#define NAME "hakit-launcher"
#define PLATFORM_URL "https://hakit.net/api/"
//#define PLATFORM_URL "http://localhost/api/"

#define LIB_DIR "/var/lib/hakit"

#define HELLO_RETRY_DELAY (3*60)

typedef enum {
	ST_IDLE=0,
	ST_HELLO,
	ST_RETRY,
	ST_APP,
	ST_ENGINE,
        NSTATES
} platform_state_t;

static platform_state_t platform_state = ST_HELLO;

static ws_client_t ws_client;

static void hello_retry(void);


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

static int create_dir(const char *dir)
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


//===================================================
// HAKit Engine
//==================================================

typedef struct {
        char *name;
        char *rev;
        char *path;
        char *basename;
        int ready;
} app_t;


static void engine_terminated(void *user_data, int status)
{
        log_str("WARNING: HAKit engine terminated with status %d", status);
	hello_retry();
}


static int engine_start(hk_tab_t *apps)
{
        char *bin = env_bindir("hakit");
        hk_proc_t *proc;
        char debug[16];
        HK_TAB_DECLARE(argv, char *);
        int i;

        log_debug(2, "engine_start");
	platform_state = ST_ENGINE;

        snprintf(debug, sizeof(debug), "--debug=%d", opt_debug);

        HK_TAB_PUSH_VALUE(argv, (char *) bin);
        HK_TAB_PUSH_VALUE(argv, (char *) debug);
        for (i = 0; i < apps->nmemb; i++) {
                app_t *app = HK_TAB_PTR(*apps, app_t, i);
                strcpy(app->basename, "app.hk");
                HK_TAB_PUSH_VALUE(argv, app->path);
        }
        HK_TAB_PUSH_VALUE(argv, (char *) NULL);

        proc = hk_proc_start_nopipe(argv.buf, NULL, engine_terminated, NULL);

        if (proc != NULL) {
                // Leave stdin stream to child
                close(STDIN_FILENO);
                log_str("HAKit engine process started: pid=%d", proc->pid);
        }
        else {
                log_str("ERROR: HAKit engine start failed");
                hello_retry();
        }

        free(bin);

	return 0;
}


//===================================================
// Application
//==================================================

typedef struct {
        hk_tab_t apps;
	int index;
} app_ctx_t;


static void app_request(app_ctx_t *ctx);


static app_ctx_t *app_ctx_alloc(void)
{
	app_ctx_t *ctx = malloc(sizeof(app_ctx_t));
        hk_tab_init(&ctx->apps, sizeof(app_t));
	ctx->index = 0;

        return ctx;
}


static void app_ctx_feed(app_ctx_t *ctx, char *str)
{
        app_t *app = hk_tab_push(&ctx->apps);

        app->name = strdup(str);
        app->rev = NULL;
        app->path = NULL;
        app->ready = 0;

        char *s = strchr(app->name, ' ');
        if (s != NULL) {
                *(s++) = '\0';
                while ((*s != '\0') && (*s <= ' ')) {
                        s++;
                }
                app->rev = s;
        }

        int size = strlen(opt_lib_dir) + strlen(app->name) + 20;
        app->path = malloc(size);
        int ofs = snprintf(app->path, size, "%s/apps", opt_lib_dir);
        create_dir(app->path);
        ofs += snprintf(app->path+ofs, size-ofs, "/%s", app->name);
        create_dir(app->path);
        app->path[ofs++] = '/';
        app->basename = &app->path[ofs];

        if (app->rev != NULL) {
                strcpy(app->basename, "REVISION");

                // Check if app is already downloaded and up-to-date
                FILE *f = fopen(app->path, "r");
                if (f != NULL) {
                        char buf[128];
                        int len = fread(buf, 1, sizeof(buf)-1, f);
                        fclose(f);

                        if (len > 0) {
                                buf[len] = '\0';
                                if (strcmp(buf, app->rev) == 0) {
                                        app->ready = 1;
                                }
                        }
                }
        }

        *(app->basename) = '\0';
}


static void app_ctx_free(app_ctx_t *ctx)
{
        int i;

        for (i = 0; i < ctx->apps.nmemb; i++) {
                app_t *app = HK_TAB_PTR(ctx->apps, app_t, i);

                free(app->name);
                free(app->path);

                memset(app, 0, sizeof(app_t));
        }

        hk_tab_cleanup(&ctx->apps);

        free(ctx);
}


static void app_response(app_ctx_t *ctx, char *buf, int len)
{
	log_debug(2, "app_response: index=%d len=%d", ctx->index, len);

        if (len < 0) {
                log_str("ERROR: Unable to connect to HAKit platform. Is network available?");
                hello_retry();
        }
        else {
                // Get error code
                char *errstr = NULL;
                int ofs = 0;
                int errcode = platform_get_status(buf, len, &errstr, &ofs);

                if (errcode == 0) {
                        app_t *app = HK_TAB_PTR(ctx->apps, app_t, ctx->index);
                        FILE *f;

                        log_str("INFO: Application '%s' downloaded successfully", app->name);

                        // Store application data
                        strcpy(app->basename, "app.hk");
                        log_str("Writing app file: %s", app->path);
                        f = fopen(app->path, "w");
                        if (f != NULL) {
                                char *buf1 = buf + ofs;
                                int len1 = len - ofs;
                                int wlen = fwrite(buf1, 1, len1, f);
                                if (wlen != len1) {
                                        log_str("ERROR: Failed to write file '%s': %s", app->path, strerror(errno));
                                        errcode = -1;
                                }
                                fclose(f);
                        }
                        else {
                                log_str("ERROR: Failed to create file '%s': %s", app->path, strerror(errno));
                                errcode = -1;
                        }

                        // Store revision tag
                        if (errcode == 0) {
                                strcpy(app->basename, "REVISION");
                                log_str("Writing app revision tag: %s", app->path);
                                f = fopen(app->path, "w");
                                if (f != NULL) {
                                        fprintf(f, "%s", app->rev);
                                        fclose(f);

                                        app->ready = 1;
                                }
                                else {
                                        log_str("ERROR: Failed to create file '%s': %s", app->path, strerror(errno));
                                        errcode = -1;
                                }
                        }
                }

                if (errcode == 0) {
                        // Ask for next application
                        ctx->index++;
                        app_request(ctx);
                }
                else {
                        hello_retry();
                }
        }
}


static void app_request(app_ctx_t *ctx)
{
	log_debug(2, "app_request %d/%d", ctx->index, ctx->apps.nmemb);
	platform_state = ST_APP;

        // Seek next appliation to download
        while (ctx->index < ctx->apps.nmemb) {
                app_t *app = HK_TAB_PTR(ctx->apps, app_t, ctx->index);
                if (app->ready) {
                        log_str("INFO: Application '%s' is up to date: %s", app->name, app->rev);
                        ctx->index++;
                }
                else {
                        log_str("INFO: Application '%s' will be downloaded: %s", app->name, app->rev);
                        break;
                }
        }

        if (ctx->index < ctx->apps.nmemb) {
                app_t *app = HK_TAB_PTR(ctx->apps, app_t, ctx->index);
                buf_t header;

                // API key
                buf_init(&header);
                platform_http_header(&header);

                // Application name
                buf_append_fmt(&header, "HAKit-Application: %s\r\n", app->name);

                ws_client_get(&ws_client, PLATFORM_URL "app.php", (char *) header.base, (ws_client_func_t *) app_response, ctx);

                buf_cleanup(&header);
        }
        else {
		engine_start(&ctx->apps);
                app_ctx_free(ctx);
	}
} 


//===================================================
// Device identification
//==================================================

static void hello_response(void *user_data, char *buf, int len)
{
	log_debug(2, "hello_response: len=%d", len);

        if (len < 0) {
                log_str("ERROR: Unable to connect to HAKit platform. Is network available?");
                hello_retry();
                //TODO: start local app if available
        }
	else if (len > 0) {
		char *errstr = NULL;
                int ofs = 0;
		int errcode = platform_get_status(buf, len, &errstr, &ofs);

		if (errcode >= 0) {
			if (errcode == 0) {
				log_str("INFO    : Device accepted by platform server");
                                char **argv = platform_get_lines(buf+ofs, len-ofs);
                                app_ctx_t *ctx = app_ctx_alloc();
                                int i;

                                for (i = 0; argv[i] != NULL; i++) {
                                        char *key = argv[i];
                                        char *value = strchr(key, ':');
                                        if (value != NULL) {
                                                *(value++) = '\0';
                                                while ((*value != '\0') && (*value <= ' ')) {
                                                        value++;
                                                }

                                                if (strcmp(key, "Application") == 0) {
                                                        app_ctx_feed(ctx, value);
                                                }
                                        }
                                }

                                free(argv);

                                app_request(ctx);
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
	platform_state = ST_RETRY;
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

        env_init(argc, argv);

        // Create lib directory
        log_debug(1, "Library path = '%s'", opt_lib_dir);
        if (create_dir(opt_lib_dir) != 0) {
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
