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


#define VERSION HAKIT_VERSION
#define NAME "hakit-launcher"
//#define PLATFORM_URL "https://hakit.net/api/"
#define PLATFORM_URL "http://localhost/api/"

#define PID_FILE "/var/run/hakit.pid"
#define LIB_DIR "/var/lib/hakit"
#define APP_FILE_NAME "app.hk"

#define HELLO_RETRY_DELAY (3*60)
#define PING_DELAY (30*60)

typedef enum {
	ST_IDLE=0,
	ST_HELLO,
	ST_RETRY,
	ST_RUN,
	ST_RESTART,
        NSTATES
} platform_state_t;

static platform_state_t platform_state = ST_HELLO;

static ws_client_t ws_client;
static io_channel_t io_stdin;

static int hello_request(void);
static void hello_retry(void);


//===================================================
// Command line arguments
//==================================================

const char *options_summary = "HAKit launcher " VERSION " (" ARCH ")";

static char *opt_pid_file = PID_FILE;
static char *opt_lib_dir = LIB_DIR;
static int opt_offline = 0;

static const options_entry_t options_entries[] = {
	{ "debug",  'd', 0,    OPTIONS_TYPE_INT,  &opt_debug,   "Set debug level", "N" },
	{ "daemon", 'D', 0,    OPTIONS_TYPE_NONE, &opt_daemon,  "Run in background as a daemon" },
	{ "pid-file", 'P', 0,  OPTIONS_TYPE_STRING, &opt_pid_file,  "Daemon PID file name (default: " PID_FILE ")", "FILE" },
	{ "lib-dir", 'L', 0,   OPTIONS_TYPE_STRING,  &opt_lib_dir,  "Lib directory to store application and config files (default: " LIB_DIR ")", "DIR" },
	{ "offline", 'l', 0,   OPTIONS_TYPE_NONE,  &opt_offline,  "Work off-line. Do not access the HAKit platform server" },
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
        buf_append_fmt(header, "HAKit-Api-Key: %s\r\n", opt_api_key);
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
// Platform ping
//==================================================

static sys_tag_t ping_timeout_tag = 0;

static void ping_start(void);

static void ping_stop(void)
{
        if (ping_timeout_tag != 0) {
                sys_remove(ping_timeout_tag);
                ping_timeout_tag = 0;
        }
}

static void ping_start(void)
{
        ping_stop();
        ping_timeout_tag = sys_timeout(PING_DELAY*1000, (sys_func_t) hello_request, NULL);
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

static hk_proc_t *engine_proc = NULL;
static HK_TAB_DECLARE(engine_argv, char *);


// Forward declaration for engine process starter
static void engine_start_now(void);


static void engine_stdout(void *user_data, char *buf, int size)
{
        fwrite(buf, 1, size, stdout);
}


static void engine_stderr(void *user_data, char *buf, int size)
{
        log_put(buf, size);
}


static void engine_terminated(void *user_data, int status)
{
        log_str("HAKit engine terminated with status %d", status);
        ping_stop();

        if (engine_proc == NULL) {
                if (platform_state == ST_RUN) {
                        engine_start_now();
                }
        }
        else {
                engine_proc = NULL;
                hello_retry();
        }
}


static void engine_start_now(void)
{
        log_debug(2, "engine_start_now");

        // Start engine process
        engine_proc = hk_proc_start(engine_argv.buf, NULL, engine_stdout, engine_stderr, engine_terminated, NULL);

        if (engine_proc != NULL) {
                platform_state = ST_RUN;

                // Leave stdin stream to child
                log_str("HAKit engine process started: pid=%d", engine_proc->pid);
                if (!opt_offline) {
                        ping_start();
                }
        }
        else {
                log_str("ERROR: HAKit engine start failed");
                hello_retry();
        }
}


static void engine_stop(void)
{
        log_debug(2, "engine_stop");

        if (engine_proc != NULL) {
                hk_proc_stop(engine_proc);
                engine_proc = NULL;
        }
}


static void engine_start(hk_tab_t *apps)
{
        int i;

        log_debug(2, "engine_start");

        // Check all application are ready
        for (i = 0; i < apps->nmemb; i++) {
                app_t *app = HK_TAB_PTR(*apps, app_t, i);
                if (!app->ready) {
                        log_str("ERROR: Cannot start engine: application '%s' is not up to date.");
                        hello_retry();
                        return;
                }
        }

        // Free old command line arguments
        for (i = 0; i < engine_argv.nmemb; i++) {
                char *args = HK_TAB_VALUE(engine_argv, char *, i);
                if (args != NULL) {
                        free(args);
                }
        }

        hk_tab_cleanup(&engine_argv);

        // Setup new command line arguments
        char *bin = env_bindir("hakit-engine");
        HK_TAB_PUSH_VALUE(engine_argv, (char *) strdup(bin));

        char debug[16];
        snprintf(debug, sizeof(debug), "--debug=%d", opt_debug);
        HK_TAB_PUSH_VALUE(engine_argv, (char *) strdup(debug));

        for (i = 0; i < apps->nmemb; i++) {
                app_t *app = HK_TAB_PTR(*apps, app_t, i);
                HK_TAB_PUSH_VALUE(engine_argv, strdup(app->path));
        }
        HK_TAB_PUSH_VALUE(engine_argv, (char *) NULL);

        // Sart HAKit engine
        if (engine_proc != NULL) {
                log_str("Engine is already running: restarting");
                platform_state = ST_RESTART;
                engine_stop();
        }
        else {
                engine_start_now();
        }
}


//===================================================
// Application
//==================================================

typedef struct {
        hk_tab_t apps;
	int index;
        int restart;
} app_ctx_t;


static void app_request(app_ctx_t *ctx);


static app_ctx_t *app_ctx_alloc(void)
{
	app_ctx_t *ctx = malloc(sizeof(app_ctx_t));
        memset(ctx, 0, sizeof(app_ctx_t));
        hk_tab_init(&ctx->apps, sizeof(app_t));

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

        strcpy(app->basename, APP_FILE_NAME);
}


static void app_ctx_feed_local(app_ctx_t *ctx, char *path)
{
        app_t *app = hk_tab_push(&ctx->apps);
        int ofs;

        app->name = NULL;
        app->rev = NULL;
        app->path = realpath(path, NULL);
        app->ready = 1;

        ofs = strlen(app->path);
        while ((ofs > 0) && (app->path[ofs-1] != '/')) {
                ofs--;
        }

        app->basename = &app->path[ofs];

        log_debug(2, "Preparing to start local application: %s", app->path);
}


static void app_ctx_free(app_ctx_t *ctx)
{
        int i;

        for (i = 0; i < ctx->apps.nmemb; i++) {
                app_t *app = HK_TAB_PTR(ctx->apps, app_t, i);

                if (app->name != NULL) {
                        free(app->name);
                }

                if (app->path != NULL) {
                        free(app->path);
                }

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
                        strcpy(app->basename, APP_FILE_NAME);
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

                                strcpy(app->basename, APP_FILE_NAME);
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

                // A new app will be downloaded, so we will need to restart the engine
                // after downloads are completed
                ctx->restart = 1;

                // API key
                buf_init(&header);
                platform_http_header(&header);

                // Application name
                buf_append_fmt(&header, "HAKit-Application: %s\r\n", app->name);

                ws_client_get(&ws_client, PLATFORM_URL "app.php", (char *) header.base, (ws_client_func_t *) app_response, ctx);

                buf_cleanup(&header);
        }
        else {
                // If engine is running and no restart is needed, keep it running
                if ((platform_state != ST_RUN) || ctx->restart) {
                        if (platform_state == ST_RUN) {
                                log_str("Updates requested by platform: restarting engine");
                        }
                        engine_start(&ctx->apps);
                }
                else {
                        log_str("No updates requested by platform: keep engine running");
                        ping_start();
                }
                app_ctx_free(ctx);
	}
} 


//===================================================
// SSL certificates
//==================================================

typedef struct {
        char *fingerprint;
        char *name;
        char *path;
} cert_req_t;

static HK_TAB_DECLARE(cert_reqs, cert_req_t);
static int cert_req_index = 0;

static void cert_request(app_ctx_t *ctx);


static void cert_reqs_cleanup(void)
{
        int i;

        for (i = 0; i < cert_reqs.nmemb; i++) {
                cert_req_t *req = HK_TAB_PTR(cert_reqs, cert_req_t, i);
                free(req->fingerprint);
                free(req->path);
        }

        hk_tab_cleanup(&cert_reqs);

        cert_req_index = 0;
}


static void cert_response(app_ctx_t *ctx, char *buf, int len)
{
	log_debug(2, "cert_response: len=%d", len);

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
                        cert_req_t *req = HK_TAB_PTR(cert_reqs, cert_req_t, cert_req_index);
                        FILE *f;

                        // Write cert data
                        //TODO: chmod 600
                        f = fopen(req->path, "w");
                        if (f != NULL) {
                                fwrite(buf+ofs, 1, len-ofs, f);
                                fclose(f);

                                // Write cert fingerprint (if any)
                                if (req->fingerprint != NULL) {
                                        char *suffix = req->path + strlen(req->path);
                                        strcpy(suffix, ".fp");

                                        f = fopen(req->path, "w");
                                        if (f != NULL) {
                                                fwrite(req->fingerprint, 1, strlen(req->fingerprint), f);
                                                fclose(f);
                                        }
                                        else {
                                                log_str("ERROR: Cannot create file '%s': %s\n", req->path, strerror(errno));
                                        }

                                        *suffix = '\0';
                                }
                        }
                        else {
                                log_str("ERROR: Cannot create file '%s': %s\n", req->path, strerror(errno));
                        }
                }

                if (errcode == 0) {
                        // Process next request
                        cert_req_index++;
                        cert_request(ctx);
                }
                else {
                        hello_retry();
                }
        }
}


static void cert_request(app_ctx_t *ctx)
{
	log_debug(2, "cert_request %d/%d", cert_req_index, cert_reqs.nmemb);

        if (cert_req_index < cert_reqs.nmemb) {
                cert_req_t *req = HK_TAB_PTR(cert_reqs, cert_req_t, cert_req_index);
                buf_t header;

                // API key
                buf_init(&header);
                platform_http_header(&header);

                // Certificate name
                buf_append_fmt(&header, "HAKit-Cert: %s\r\n", req->name);

                ws_client_get(&ws_client, PLATFORM_URL "cert.php", (char *) header.base, (ws_client_func_t *) cert_response, ctx);

                buf_cleanup(&header);
        }
        else {
                cert_reqs_cleanup();
                app_request(ctx);
        }
}


static int cert_check(char *str)
{
        // Extract cert name and fingerprint
        char *fp = strchr(str, ' ');
        if (fp == NULL) {
                return 0;
        }
        *(fp++) = '\0';

        // Build cert file path
        int size = strlen(opt_lib_dir) + strlen(str) + 20;
        char *path = malloc(size);
        int ofs = snprintf(path, size, "%s/certs/", opt_lib_dir);
        int dir_len = ofs;
        create_dir(path);

        ofs += snprintf(path+ofs, size-ofs, "%s", str);

        // Check if cert fingerprint has changed
        snprintf(path+ofs, size-ofs, ".fp");
        FILE *f = fopen(path, "r");
        if (f != NULL) {
                char buf[128];
                int len = fread(buf, 1, sizeof(buf)-1, f);
                fclose(f);
                buf[len] = '\0';

                // If fingerprint is the same than previously, do not request cert download
                if (strcmp(buf, fp) == 0) {
                        log_str("SSL certificate '%s' is up to date", str);
                        free(path);
                        return 0;
                }

                // Fingerprint file is not up to date, so delete it
                unlink(path);
        }

        path[ofs] = '\0';

        // Cert is not present or fingerprint has changed: request download
        log_str("SSL certificate '%s' needs to be downloaded", str);

        cert_req_t *req = hk_tab_push(&cert_reqs);
        req->fingerprint = strdup(fp);
        req->path = path;
        req->name = &path[dir_len];

        // If the server certificate has changed, also request the server key
        if (strcmp(str, "server.crt") == 0) {
                cert_req_t *req = hk_tab_push(&cert_reqs);
                req->fingerprint = NULL;
                req->path = strdup(path);
                req->name = &req->path[dir_len];
                strcpy(req->name, "server.key");
        }

        return 1;
}


//===================================================
// Device identification
//==================================================

static int hello_cache_save(char **argv)
{
        char fname[strlen(opt_lib_dir) + 8];
        FILE *f;
        int i;

        snprintf(fname, sizeof(fname), "%s/HELLO", opt_lib_dir);

        f = fopen(fname, "w");
        if (f == NULL) {
                log_str("WARNING: Cannot save HELLO cache file '%s': %s", fname, strerror(errno));
                return -1;
        }

        for (i = 0; argv[i] != NULL; i++) {
                fputs(argv[i], f);
                fputc('\n', f);
        }

        fclose(f);

        return 0;
}


static void hello_response_parse(char *str, app_ctx_t *ctx)
{
        char *value = strchr(str, ':');

        if (value != NULL) {
                *(value++) = '\0';
                while ((*value != '\0') && (*value <= ' ')) {
                        value++;
                }

                if (strcmp(str, "Application") == 0) {
                        app_ctx_feed(ctx, value);
                }
                else if (strcmp(str, "Cert") == 0) {
                        cert_check(value);
                }
        }
}


static app_ctx_t *hello_cache_load(void)
{
        char fname[strlen(opt_lib_dir) + 8];
        app_ctx_t *ctx = NULL;
        FILE *f;

        snprintf(fname, sizeof(fname), "%s/HELLO", opt_lib_dir);

        f = fopen(fname, "r");
        if (f == NULL) {
                log_str("WARNING: Cannot load HELLO cache file '%s': %s", fname, strerror(errno));
                return NULL;
        }

        log_str("INFO: Loading HELLO cache file");
        ctx = app_ctx_alloc();

        while (!feof(f)) {
                char buf[128];
                if (fgets(buf, sizeof(buf), f) != NULL) {
                        int len = strlen(buf);
                        while ((len > 0) && (buf[len-1] <= ' ')) {
                                len--;
                        }
                        buf[len] = '\0';

                        hello_response_parse(buf, ctx);
                }
        }

        fclose(f);

        return ctx;
}


static void hello_response(void *user_data, char *buf, int len)
{
	log_debug(2, "hello_response: len=%d", len);

        if (len < 0) {
                log_str("ERROR: Unable to connect to HAKit platform. Is network available?");

                app_ctx_t *ctx = hello_cache_load();
                if (ctx != NULL) {
                        // Start engine from cached program, except if it is already running
                        if (platform_state != ST_RUN) {
                                engine_start(&ctx->apps);
                        }
                        app_ctx_free(ctx);
                }
                else {
                        hello_retry();
                }
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

                                hello_cache_save(argv);
                                cert_reqs_cleanup();

                                for (i = 0; argv[i] != NULL; i++) {
                                        hello_response_parse(argv[i], ctx);
                                }

                                free(argv);

                                cert_request(ctx);
			}
			else {
				log_str("WARNING : Access denied by platform server: %s", errstr);
                                platform_state = ST_HELLO;

                                // Kill currently running engine if access is denied by platform
                                engine_stop();
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

	// API key
        buf_init(&header);
	platform_http_header(&header);

        if (platform_state != ST_RUN) {
                platform_state = ST_HELLO;

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
        }

	ws_client_get(&ws_client, PLATFORM_URL "hello.php", (char *) header.base, hello_response, NULL);

        buf_cleanup(&header);

	return 0;
}


static void hello_retry(void)
{
        platform_state = ST_RETRY;

        if (opt_api_key != NULL) {
                log_str("INFO    : New HELLO attempt in %d seconds", HELLO_RETRY_DELAY);
                sys_timeout(HELLO_RETRY_DELAY*1000, (sys_func_t) hello_request, NULL);
        }
        else {
                log_str("ERROR: Engine terminated in off-line mode: exiting.");
                exit(2);
        }
}


//===================================================
// Program body
//===================================================

static void goodbye(void *user_data)
{
        log_debug(3, "Deleting PID file '%s'", opt_pid_file);
        if (unlink(opt_pid_file) < 0) {
                log_str("ERROR: Cannot delete pid file '%s': %s\n", opt_pid_file, strerror(errno));
        }
}


static void write_pid_file(pid_t pid)
{
        FILE *f = fopen(opt_pid_file, "w");
        if (f != NULL) {
                fprintf(f, "%d\n", pid);
                fclose(f);
                log_debug(3, "PID file '%s' created", opt_pid_file);
        }
        else {
                log_str("ERROR: Cannot create pid file '%s': %s\n", opt_pid_file, strerror(errno));
        }
}


static void run_as_daemon(void)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		log_str("ERROR: Fork failed: %s", strerror(errno));
		exit(3);
	}

	if (pid > 0) {
                write_pid_file(pid);
		exit(0);
	}

        sys_quit_handler((sys_func_t) goodbye, NULL);

	if (setsid() < 0) {
		log_str("ERROR: Setsid failed: %s", strerror(errno));
		exit(3);
 	}

	close(STDIN_FILENO);

	log_str("Starting in daemon mode: pid=%d", getpid());
}


static int stdin_recv(void *user_data, char *buf, int len)
{
	if (buf == NULL) {
		/* Quit if hangup from stdin */
		sys_quit();
                return 0;
        }

        if (engine_proc != NULL) {
                hk_proc_write(engine_proc, buf, len);

                while ((len > 0) && (buf[len-1] < ' ')) {
                        buf[len--] = '\0';
                }
        }
        else {
                log_str("WARNING: Engine not running - Input data ignored");
        }

        return 1;
}


int main(int argc, char *argv[])
{
        char *app;

	if (options_parse(options_entries, &argc, argv) != 0) {
		exit(1);
	}

	/* Init exec environment */
        env_init(argc, argv);

	if (opt_daemon) {
		run_as_daemon();
	}

	/* Init log management */
	log_init(NAME);
	log_str(options_summary);

        /* If a local application is given in command line arguments, force off-line mode */
        app = env_app();
        if (app != NULL) {
                log_str("Using local application: off-line mode forced");
                opt_offline = 1;
        }

        /* Get platform settings if working on-line */
        if (!opt_offline) {
                options_conf_parse(api_auth_entries, "platform");
        }

        /* Enable per-line output buffering */
        setlinebuf(stdout);

	/* Init system runtime */
	sys_init();

	/* Setup stdin command handler if not running as a daemon */
	if (!opt_daemon) {
		io_channel_setup(&io_stdin, STDIN_FILENO, (io_func_t) stdin_recv, NULL);
	}

	// Setup WS HTTP client
	memset(&ws_client, 0, sizeof(ws_client));
	if (ws_client_init(&ws_client, 1) < 0) {
		log_str("ERROR: Failed to init HTTP client");
		exit(1);
	}

        /* Start it all, either on- or off-line */
        if (app != NULL) {
                /* Start local application, if any */
                app_ctx_t *ctx = app_ctx_alloc();
                app_ctx_feed_local(ctx, app);
		engine_start(&ctx->apps);
                app_ctx_free(ctx);
        }
        else {
                /* Create lib directory */
                log_debug(1, "Library path = '%s'", opt_lib_dir);
                if (create_dir(opt_lib_dir) != 0) {
                        exit(2);
                }

                if (opt_api_key != NULL) {
                        /* Advertise device */
                        hello_request();
                }
                else {
                        /* Off-line mode: try to get application from the cache */
                        app_ctx_t *ctx = hello_cache_load();
                        if (ctx == NULL) {
                                log_str("ERROR: No API-Key provided, No application found in the cache: exiting.");
                                exit(2);
                        }
                }
        }

	sys_run();

	return 0;
}
