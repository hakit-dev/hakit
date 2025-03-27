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
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/stat.h>

#include "types.h"
#include "options.h"
#include "log.h"
#include "sys.h"
#include "env.h"
#include "buf.h"
#include "tab.h"
#include "proc.h"
#include "ws_client.h"

#include "hakit_version.h"


#define VERSION HAKIT_VERSION
#define NAME "hakit-launcher"
#define PLATFORM_URL "https://hakit.net"

#define PID_FILE "/var/run/hakit.pid"
#define LIB_DIR "/var/lib/hakit"
#define TILE_FILE_NAME "tile.hk"

#define HELLO_RETRY_INIT_DELAY 30
#define HELLO_RETRY_DELAY (3*60)
#define PING_DELAY (30*60)
#define MQTT_RETRY_DELAY 10
#define MQTT_MAX_RETRIES 3
#define MQTT_PORT 8883

typedef enum {
	ST_IDLE=0,
	ST_HELLO,
	ST_RETRY,
	ST_RUN,
	ST_RESTART,
        NSTATES
} state_t;

static state_t state = ST_HELLO;
static int running_fallback = 0;

static ws_client_t ws_client;
static io_channel_t io_stdin;

static int hello_request(void);
static void hello_retry(void);


//===================================================
// Command line arguments
//==================================================

const char *options_summary = "HAKit launcher " VERSION " (" ARCH ")";

static char *opt_pid_file = PID_FILE;
static char *opt_platform_url = PLATFORM_URL;
static char *opt_lib_dir = NULL;
static int opt_offline = 0;
static int opt_no_advertise = 0;
static int opt_no_hkcp = 0;
#ifdef WITH_MQTT
static int opt_no_mqtt = 0;
static int opt_mqtt_port = MQTT_PORT;
#endif
static int opt_no_ssl = 0;
static char *opt_tile = NULL;
static char *opt_http_alias = NULL;
static int opt_full_name = 0;

static const options_entry_t options_entries[] = {
	{ "debug",        'd', OPTION_FLAG_NONE, OPTIONS_TYPE_INT,    &opt_debug,        "Set debug level", "N" },
	{ "daemon",       'D', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_daemon,       "Run in background as a daemon" },
	{ "pid-file",     'P', OPTION_FLAG_NONE, OPTIONS_TYPE_STRING, &opt_pid_file,     "Daemon PID file name (default: " PID_FILE ")", "FILE" },
	{ "platform-url", 'U', OPTION_FLAG_NONE, OPTIONS_TYPE_STRING, &opt_platform_url, "HAKit web platform URL (default: " PLATFORM_URL ")", "URL" },
	{ "lib-dir",      'L', OPTION_FLAG_NONE, OPTIONS_TYPE_STRING, &opt_lib_dir,      "Lib directory to store tiles and config files (default: " LIB_DIR ")", "DIR" },
	{ "offline",      'l', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_offline,      "Work off-line. Do not access the HAKit platform server" },
	{ "no-advertise", 'N', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_no_advertise, "Disable HKCP/MQTT advertising" },
	{ "no-hkcp",      'n', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_no_hkcp,      "Disable HKCP protocol" },
#ifdef WITH_MQTT
	{ "no-mqtt",      'm', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_no_mqtt,      "Disable MQTT protocol" },
	{ "mqtt-port",    'p', OPTION_FLAG_NONE, OPTIONS_TYPE_INT,    &opt_mqtt_port,    "MQTT broker port number (default: " xstr(MQTT_PORT) ")", "PORT" },
#endif
	{ "no-ssl",       's', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_no_ssl,       "Disable SSL for all engine protocols (HKCP, MQTT and HTTP)" },
	{ "http-alias",   'a', OPTION_FLAG_LIST, OPTIONS_TYPE_STRING, &opt_http_alias,   "Set list of HTTP alias to file paths", "ALIAS=DIR,..." },
	{ "tile",         't', OPTION_FLAG_LIST, OPTIONS_TYPE_STRING, &opt_tile,         "Set list of local tiles (implies --offline)", "TILE,..." },
	{ "full-name",    'f', OPTION_FLAG_NONE, OPTIONS_TYPE_NONE,   &opt_full_name,    "Use fully qualified endpoint names. Do not connect local sinks/sources together." },
	{ NULL }
};


//===================================================
// Environment
//===================================================

static int create_dir(const char *dir, unsigned int mode)
{
        if (mkdir(dir, mode) == 0) {
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
	{ "api-key", 'K', 0,   OPTIONS_TYPE_STRING,   &opt_api_key,  "API key for accessing HAKit web platform" },
};


static int platform_request(char *script, buf_t *header, ws_client_func_t *callback, void *callback_data)
{
	// Construct request URL
	char url[strlen(opt_platform_url) + strlen(script) + 8];
	snprintf(url, sizeof(url), "%s/api/%s", opt_platform_url, script);

        // Add API key in HTTP header
        buf_append_fmt(header, "HAKit-Api-Key: %s\r\n", opt_api_key);

	int ret = ws_client_get(&ws_client, url, (char *) header->base, callback, callback_data);

	buf_cleanup(header);

        return ret;
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
// Context descriptors
//==================================================

typedef struct {
        char *name;
        char *rev;
        char *path;
        char *basename;
        int ready;
} tile_t;

typedef struct {
        char *fingerprint;
        char *name;
        char *path;
} cert_t;

typedef struct {
        hk_tab_t tiles;
        hk_tab_t certs;
	int index;
        int restart_engine;
#ifdef WITH_MQTT
        int is_broker;
        int restart_broker;
#endif /* WITH_MQTT */
} ctx_t;


static ctx_t *ctx_alloc(void)
{
	ctx_t *ctx = malloc(sizeof(ctx_t));
        memset(ctx, 0, sizeof(ctx_t));
        hk_tab_init(&ctx->tiles, sizeof(tile_t));
        hk_tab_init(&ctx->certs, sizeof(cert_t));

        return ctx;
}


static void ctx_tile_feed(ctx_t *ctx, char *str)
{
        tile_t *tile = hk_tab_push(&ctx->tiles);

        tile->name = strdup(str);
        tile->rev = NULL;
        tile->path = NULL;
        tile->ready = 0;

        char *s = strchr(tile->name, ' ');
        if (s != NULL) {
                *(s++) = '\0';
                while ((*s != '\0') && (*s <= ' ')) {
                        s++;
                }
                tile->rev = s;
        }

        int size = strlen(opt_lib_dir) + strlen(tile->name) + 20;
        tile->path = malloc(size);
        int ofs = snprintf(tile->path, size, "%s/tiles", opt_lib_dir);
        create_dir(tile->path, 0755);
        ofs += snprintf(tile->path+ofs, size-ofs, "/%s", tile->name);
        create_dir(tile->path, 0755);
        tile->path[ofs++] = '/';
        tile->basename = &tile->path[ofs];

        if (tile->rev != NULL) {
                strcpy(tile->basename, "REVISION");

                // Check if tile is already downloaded and up-to-date
                FILE *f = fopen(tile->path, "r");
                if (f != NULL) {
                        char buf[128];
                        int len = fread(buf, 1, sizeof(buf)-1, f);
                        fclose(f);

                        if (len > 0) {
                                buf[len] = '\0';
                                if (strcmp(buf, tile->rev) == 0) {
                                        tile->ready = 1;
                                }
                        }
                }
        }

        tile->basename[0] = '\0';
}


static void ctx_tile_feed_local(ctx_t *ctx, char *path)
{
        char *name = NULL;
        char *path1 = strchr(path, '=');
        if (path1 != NULL) {
                name = path;
                *(path1++) = '\0';
        }
        else {
                path1 = path;
        }

        char *rpath = realpath(path1, NULL);
        if (rpath == NULL) {
                log_str("ERROR: Local tile '%s' not found", path1);
                return;
        }

        tile_t *tile = hk_tab_push(&ctx->tiles);
        int ofs;

        if (name != NULL) {
                tile->name = strdup(name);
        }
        else {
                tile->name = NULL;
        }
        tile->rev = NULL;
        tile->path = rpath;
        tile->ready = 1;

        ofs = strlen(tile->path);
        while ((ofs > 0) && (tile->path[ofs-1] != '/')) {
                ofs--;
        }

        tile->basename = &tile->path[ofs];

        if (name != NULL) {
                log_debug(2, "Preparing to start local tile '%s' as '%s'", tile->path, tile->name);
        }
        else {
                log_debug(2, "Preparing to start local tile '%s'", tile->path);
        }
}


static void ctx_tile_cleanup(ctx_t *ctx)
{
        int i;

        for (i = 0; i < ctx->tiles.nmemb; i++) {
                tile_t *tile = HK_TAB_PTR(ctx->tiles, tile_t, i);

                if (tile->name != NULL) {
                        free(tile->name);
                }

                if (tile->path != NULL) {
                        free(tile->path);
                }

                memset(tile, 0, sizeof(tile_t));
        }

        hk_tab_cleanup(&ctx->tiles);
}


static void ctx_cert_cleanup(ctx_t *ctx)
{
        int i;

        for (i = 0; i < ctx->certs.nmemb; i++) {
                cert_t *crt = HK_TAB_PTR(ctx->certs, cert_t, i);

                if (crt->fingerprint != NULL) {
                        free(crt->fingerprint);
                }

                if (crt->path != NULL) {
                        free(crt->path);
                }
        }

        hk_tab_cleanup(&ctx->certs);
}


static void ctx_free(ctx_t *ctx)
{
        ctx_tile_cleanup(ctx);
        ctx_cert_cleanup(ctx);
        memset(ctx, 0, sizeof(ctx_t));
        free(ctx);
}


//===================================================
// Platform ping
//==================================================

static sys_tag_t ping_timeout_tag = 0;

static void ping_stop(void)
{
        log_debug(2, "ping_stop");
        if (ping_timeout_tag != 0) {
                sys_remove(ping_timeout_tag);
                ping_timeout_tag = 0;
        }
}

static void ping_start(void)
{
        ping_stop();
        log_debug(2, "ping_start");
        ping_timeout_tag = sys_timeout(PING_DELAY*1000, (sys_func_t) hello_request, NULL);
}


//===================================================
// HAKit Engine
//==================================================

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
                if (state == ST_RESTART) {
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
        int i;

        log_debug(2, "engine_start_now");
        for (i = 0; i < engine_argv.nmemb; i++) {
                char *args = HK_TAB_VALUE(engine_argv, char *, i);
                if (args != NULL) {
                        log_debug(2, "  [%d]='%s'", i, args);
                }
        }

        // Start engine process
        engine_proc = hk_proc_start(engine_argv.buf, NULL, engine_stdout, engine_stderr, engine_terminated, NULL);

        if (engine_proc != NULL) {
                state = ST_RUN;

                // Leave stdin stream to child
                log_str("HAKit engine process started: pid=%d", engine_proc->pid);
                if ((!opt_offline) && (!running_fallback)) {
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


static void engine_start(ctx_t *ctx)
{
        int i;

        log_debug(2, "engine_start");

        // Check all tiles are ready
        for (i = 0; i < ctx->tiles.nmemb; i++) {
                tile_t *tile = HK_TAB_PTR(ctx->tiles, tile_t, i);
                if (!tile->ready) {
                        log_str("ERROR: Cannot start engine: tile '%s' is not up to date.");
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

        if (opt_debug > 0) {
                char debug[20];
                snprintf(debug, sizeof(debug), "--debug=%d", opt_debug);
                HK_TAB_PUSH_VALUE(engine_argv, (char *) strdup(debug));
        }

        if (opt_no_ssl) {
                HK_TAB_PUSH_VALUE(engine_argv, strdup("--no-https"));
        }
        else {
                char args[strlen(opt_lib_dir)+16];
                snprintf(args, sizeof(args), "--certs=%s/certs", opt_lib_dir);
                HK_TAB_PUSH_VALUE(engine_argv, (char *) strdup(args));
        }

#ifdef WITH_MQTT
	if (opt_no_mqtt) {
		HK_TAB_PUSH_VALUE(engine_argv, strdup("--no-mqtt"));
	}
        else {
                if (ctx->is_broker) {
                        char hostname[64];
                        char args[128];

                        gethostname(hostname, sizeof(hostname));

                        snprintf(args, sizeof(args), "--mqtt-broker=%s:%d", hostname, opt_mqtt_port);
                        HK_TAB_PUSH_VALUE(engine_argv, (char *) strdup(args));
                }
        }
#endif

	if (opt_no_hkcp) {
		HK_TAB_PUSH_VALUE(engine_argv, strdup("--no-hkcp"));
	}

	if (opt_no_advertise) {
		HK_TAB_PUSH_VALUE(engine_argv, strdup("--no-advertise"));
	}

	if (opt_full_name) {
		HK_TAB_PUSH_VALUE(engine_argv, strdup("--full-name"));
	}

	if (opt_http_alias != NULL) {
                int size = 14 + strlen(opt_http_alias);
                char *str = malloc(size);
                snprintf(str, size, "--http-alias=%s", opt_http_alias);
		HK_TAB_PUSH_VALUE(engine_argv, str);
	}

        for (i = 0; i < ctx->tiles.nmemb; i++) {
                tile_t *tile = HK_TAB_PTR(ctx->tiles, tile_t, i);
                int size = strlen(tile->path) + 1;
                if (tile->name != NULL) {
                        size += strlen(tile->name) + 1;
                }
                char *str = malloc(size);
                int len = 0;
                if (tile->name != NULL) {
                        len += snprintf(str+len, size-len, "%s=", tile->name);
                }
                len += snprintf(str+len, size-len, "%s", tile->path);
                HK_TAB_PUSH_VALUE(engine_argv, str);
        }

        HK_TAB_PUSH_VALUE(engine_argv, (char *) NULL);

        // Start engine. Restart if it is already running
        if (engine_proc != NULL) {
                log_str("Engine is already running: restarting");
                state = ST_RESTART;
                engine_stop();
        }
        else {
                engine_start_now();
        }
}


//===================================================
// Application
//==================================================

static void app_request(ctx_t *ctx);


static void app_response(ctx_t *ctx, char *buf, int len)
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
                        tile_t *tile = HK_TAB_PTR(ctx->tiles, tile_t, ctx->index);
                        FILE *f;

                        log_str("INFO: Tile '%s' downloaded successfully", tile->name);

                        // Store tile data
                        strcpy(tile->basename, TILE_FILE_NAME);
                        log_str("Writing tile file: %s", tile->path);
                        f = fopen(tile->path, "w");
                        if (f != NULL) {
                                char *buf1 = buf + ofs;
                                int len1 = len - ofs;
                                int wlen = fwrite(buf1, 1, len1, f);
                                if (wlen != len1) {
                                        log_str("ERROR: Failed to write file '%s': %s", tile->path, strerror(errno));
                                        errcode = -1;
                                }
                                fclose(f);
                        }
                        else {
                                log_str("ERROR: Failed to create file '%s': %s", tile->path, strerror(errno));
                                errcode = -1;
                        }

                        // Store revision tag
                        if (errcode == 0) {
                                strcpy(tile->basename, "REVISION");

                                log_str("Writing tile revision tag: %s", tile->path);
                                f = fopen(tile->path, "w");
                                if (f != NULL) {
                                        fprintf(f, "%s", tile->rev);
                                        fclose(f);

                                        tile->ready = 1;
                                }
                                else {
                                        log_str("ERROR: Failed to create file '%s': %s", tile->path, strerror(errno));
                                        errcode = -1;
                                }

                                tile->basename[0] = '\0';
                        }
                }

                if (errcode == 0) {
                        // Ask for next tile
                        ctx->index++;
                        app_request(ctx);
                }
                else {
                        hello_retry();
                }
        }
}


static void app_request(ctx_t *ctx)
{
	log_debug(2, "app_request %d/%d", ctx->index, ctx->tiles.nmemb);

        // Seek next tile to download
        while (ctx->index < ctx->tiles.nmemb) {
                tile_t *tile = HK_TAB_PTR(ctx->tiles, tile_t, ctx->index);
                if (tile->ready) {
                        log_str("INFO: Tile '%s' is up to date: %s", tile->name, tile->rev);
                        ctx->index++;
                }
                else {
                        log_str("INFO: Tile '%s' will be downloaded: %s", tile->name, tile->rev);
                        break;
                }
        }

        if (ctx->index < ctx->tiles.nmemb) {
                tile_t *tile = HK_TAB_PTR(ctx->tiles, tile_t, ctx->index);
                buf_t header;

                // A new tile will be downloaded, so we will need to restart the engine
                // after downloads are completed
                ctx->restart_engine = 1;

                buf_init(&header);

                // Tile name
                buf_append_fmt(&header, "HAKit-Tile: %s\r\n", tile->name);

		platform_request("app.php", &header, (ws_client_func_t *) app_response, ctx);
        }
        else {
                // If engine is running and no restart is needed, keep it running
                if ((state != ST_RUN) || ctx->restart_engine) {
                        if (state == ST_RUN) {
                                log_str("Updates requested by platform: restarting engine");
                        }
                        engine_start(ctx);
                }
                else {
                        log_str("No updates requested by platform: keeping engine running");
                        ping_start();
                }
                ctx_free(ctx);
	}
} 


//===================================================
// MQTT Broker
//==================================================

#ifdef WITH_MQTT

static hk_proc_t *mqtt_proc = NULL;
static int mqtt_restart = 0;
static int mqtt_retry_count = 0;

static int mqtt_start_now(void);

static char *mqtt_dir(char *filename)
{
	int size = strlen(opt_lib_dir) + strlen(filename) + 8;
	char *path = malloc(size);
	snprintf(path, size, "%s/mqtt/%s", opt_lib_dir, filename);
	return path;
}


static char *mqtt_config(void)
{
	return mqtt_dir("mosquitto.conf");
}


static void mqtt_stdxxx(char *tag, char *buf, int size)
{
	char tag_str[9];
	int len;
	int i;

	len = strlen(tag);
	for (i = 0; i < (sizeof(tag_str)-1); i++) {
		if (i < len) {
			tag_str[i] = tag[i];
		}
		else {
			tag_str[i] = ' ';
		}
	}
	len = i;
	tag_str[len] = '\0';
	
	i = 0;
	while (i < size) {
		int i0 = i;

		log_tstamp();
		log_put("MQTT-broker ", 12);
		log_put(tag_str, len);

		while ((i < size) && (buf[i] != '\n')) {
			i++;
		}
		if (buf[i] == '\n') {
			i++;
		}
		log_put(&buf[i0], i-i0);
	}
}


static void mqtt_stdout(void *user_data, char *buf, int size)
{
	mqtt_stdxxx("STDOUT", buf, size);
}


static void mqtt_stderr(void *user_data, char *buf, int size)
{
	mqtt_stdxxx("STDERR", buf, size);
}


static void mqtt_terminated(void *user_data, int status)
{
        log_str("MQTT broker terminated with status %d", status);

        if (mqtt_proc == NULL) {
		// Broker intentionally killed for restart
		if (mqtt_restart) {
			mqtt_restart = 0;
			mqtt_start_now();
		}
        }
        else {
		// Broker terminated without being intentionally killed
		mqtt_proc = NULL;

		mqtt_retry_count++;
		if (mqtt_retry_count <= MQTT_MAX_RETRIES) {
			log_str("PANIC: MQTT broker terminated (%d/%d). Restarting in %d seconds ...", mqtt_retry_count, MQTT_MAX_RETRIES, MQTT_RETRY_DELAY);
			sys_timeout(MQTT_RETRY_DELAY*1000, (sys_func_t) mqtt_start_now, NULL);
		}
		else {
			log_str("PANIC: MQTT broker terminated after %d restart attempts. Giving up.", MQTT_MAX_RETRIES);
		}
        }
}


static int mqtt_start_now(void)
{
	char *config = mqtt_config();
	char *argv[] = {
		"mosquitto",
		"-c", config,
		NULL,
		NULL
	};

        log_debug(2, "mqtt_start_now");

	if (opt_debug > 0) {
		argv[3] = "-v";
	}

        // Start broker process
        mqtt_proc = hk_proc_start(argv, NULL, mqtt_stdout, mqtt_stderr, mqtt_terminated, NULL);

        if (mqtt_proc != NULL) {
                // Leave stdin stream to child
                log_str("MQTT broker process started: pid=%d", mqtt_proc->pid);
        }
        else {
                log_str("ERROR: MQTT broker start failed");
        }

	free(config);

	return 0;
}


static void mqtt_stop(int restart)
{
        log_debug(2, "mqtt_stop");

        if (mqtt_proc != NULL) {
		mqtt_restart = restart;
                hk_proc_stop(mqtt_proc);
                mqtt_proc = NULL;
        }
}


static void mqtt_start(void)
{
        log_debug(2, "mqtt_start");

        // Start broker. Restart if it is already running
        if (mqtt_proc != NULL) {
                log_str("MQTT broker is already running: restarting");
                mqtt_stop(1);
        }
        else {
                mqtt_start_now();
        }
}


void mqtt_write_config(ctx_t *ctx)
{
	char *dir = mqtt_dir("");
        create_dir(dir, 0755);

	char *config = mqtt_config();
        log_str("Writing MQTT broker config file: %s", config);

        FILE *f = fopen(config, "w");
        fprintf(f, "port %d\n", opt_mqtt_port);
        fprintf(f, "persistence_location %s\n", dir);
        fprintf(f, "cafile %s/certs/ca.crt\n", opt_lib_dir);
        fprintf(f, "keyfile %s/certs/server.key\n", opt_lib_dir);
        fprintf(f, "certfile %s/certs/server.crt\n", opt_lib_dir);
        fclose(f);

	free(config);
	free(dir);
}

#endif /* WITH_MQTT */


//===================================================
// SSL certificates
//==================================================

static void cert_request(ctx_t *ctx);


static void cert_response(ctx_t *ctx, char *buf, int len)
{
	log_debug(2, "cert_response: index=%d len=%d", ctx->index, len);

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
                        cert_t *crt = HK_TAB_PTR(ctx->certs, cert_t, ctx->index);

                        // Write cert data
			int fd = open(crt->path, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
			if (fd >= 0) {
                                int ret = write(fd, buf+ofs, len-ofs);
				if (ret < 0) {
					log_str("ERROR: Cannot write file '%s': %s\n", crt->path, strerror(errno));
				}
                                close(fd);

                                // Write cert fingerprint (if any)
                                if (crt->fingerprint != NULL) {
                                        char *suffix = crt->path + strlen(crt->path);
                                        strcpy(suffix, ".fp");

                                        FILE *f = fopen(crt->path, "w");
                                        if (f != NULL) {
                                                fwrite(crt->fingerprint, 1, strlen(crt->fingerprint), f);
                                                fclose(f);
                                        }
                                        else {
                                                log_str("ERROR: Cannot create file '%s': %s\n", crt->path, strerror(errno));
                                        }

                                        *suffix = '\0';
                                }
                        }
                        else {
                                log_str("ERROR: Cannot create file '%s': %s\n", crt->path, strerror(errno));
                        }
                }

                if (errcode == 0) {
                        // Process next request
                        ctx->index++;
                        cert_request(ctx);
                }
                else {
                        hello_retry();
                }
        }
}


static void cert_request(ctx_t *ctx)
{
	log_debug(2, "cert_request %d/%d", ctx->index, ctx->certs.nmemb);

        if (ctx->index < ctx->certs.nmemb) {
                cert_t *crt = HK_TAB_PTR(ctx->certs, cert_t, ctx->index);
                buf_t header;

		log_str("Downloading SSL certificate '%s'", crt->name);

                // A new certificate will be downloaded : request to restart engine and MQTT broker
                ctx->restart_engine = 1;

#ifdef WITH_MQTT
                if (!opt_no_mqtt) {
                        if (ctx->is_broker) {
                                ctx->restart_broker = 1;
                        }
                }
#endif /* WITH_MQTT */

                buf_init(&header);

                // Certificate name
                buf_append_fmt(&header, "HAKit-Cert: %s\r\n", crt->name);

		platform_request("cert.php", &header, (ws_client_func_t *) cert_response, ctx);
        }
        else {
#ifdef WITH_MQTT
                if (!opt_no_mqtt) {
                        // Start MQTT broker
                        if (ctx->is_broker) {
                                if (mqtt_proc == NULL) {
                                        log_str("Starting MQTT broker");
                                        mqtt_start();
                                }
                                else {
                                        if (ctx->restart_broker) {
                                                log_str("SSL certificates updated: restarting MQTT broker");
                                                mqtt_stop(1);
                                        }
                                        else {
                                                log_str("SSL certificates unchanged: keeping MQTT broker running");
                                        }
                                }
                        }
                        else {
                                mqtt_stop(0);
                        }
                }
#endif /* WITH_MQTT */
		
		// Fetch tiles
                ctx->index = 0;
                app_request(ctx);
        }
}


static int cert_check(ctx_t *ctx, char *str)
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
        create_dir(path, 0700);

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

        cert_t *crt = hk_tab_push(&ctx->certs);
        crt->fingerprint = strdup(fp);
        crt->path = path;
        crt->name = &path[dir_len];

        // If the server certificate has changed, also request the server key
        if (strcmp(str, "server.crt") == 0) {
                cert_t *crt = hk_tab_push(&ctx->certs);
                crt->fingerprint = NULL;
                crt->path = strdup(path);
                crt->name = &crt->path[dir_len];
                strcpy(crt->name, "server.key");
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


static void hello_response_parse(char *str, ctx_t *ctx)
{
        char *value = strchr(str, ':');

        if (value != NULL) {
                *(value++) = '\0';
                while ((*value != '\0') && (*value <= ' ')) {
                        value++;
                }
        }

        if (strcmp(str, "Tile") == 0) {
                if (value != NULL) {
                        ctx_tile_feed(ctx, value);
                }
        }
        else if (strcmp(str, "Cert") == 0) {
                if (value != NULL) {
                        cert_check(ctx, value);
                }
        }
#ifdef WITH_MQTT
        else if (strcmp(str, "MQTT-broker") == 0) {
                if (!opt_no_mqtt) {
                        ctx->is_broker = 1;
                        mqtt_write_config(ctx);
                }
        }
#endif /* WITH_MQTT */
}


static ctx_t *hello_cache_load(void)
{
        char fname[strlen(opt_lib_dir) + 8];
        ctx_t *ctx = NULL;
        FILE *f;

        snprintf(fname, sizeof(fname), "%s/HELLO", opt_lib_dir);

        f = fopen(fname, "r");
        if (f == NULL) {
                log_str("WARNING: Cannot load HELLO cache file '%s': %s", fname, strerror(errno));
                return NULL;
        }

        log_str("INFO: Loading HELLO cache file");
        ctx = ctx_alloc();

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

                // Start engine from cached program, except if it is already running
                if (state != ST_RUN) {
                        ctx_t *ctx = hello_cache_load();
                        if (ctx != NULL) {
                                log_str("Starting engine from cached settings as temporary fallback ...");
                                running_fallback = 1;
                                engine_start(ctx);
                                ctx_free(ctx);
                        }
                }

                hello_retry();
        }
	else if (len > 0) {
		char *errstr = NULL;
                int ofs = 0;
		int errcode = platform_get_status(buf, len, &errstr, &ofs);

		if (errcode >= 0) {
                        if (errcode == 0) {
				log_str("INFO    : Device accepted by platform server");
                                char **argv = platform_get_lines(buf+ofs, len-ofs);
                                ctx_t *ctx = ctx_alloc();
                                int i;

                                hello_cache_save(argv);

                                for (i = 0; argv[i] != NULL; i++) {
                                        hello_response_parse(argv[i], ctx);
                                }

                                free(argv);

                                ctx->index = 0;
                                cert_request(ctx);
			}
			else {
				log_str("WARNING : Access denied by platform server: %s", errstr);
                                state = ST_HELLO;

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

        if ((state != ST_RUN) || running_fallback) {
		if (state != ST_RUN) {
			state = ST_HELLO;
		}
                running_fallback = 0;

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

	return platform_request("hello.php", &header, (ws_client_func_t *) hello_response, NULL);
}


static void hello_retry(void)
{
        state = ST_RETRY;

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
	if (options_parse(options_entries, "launcher*", &argc, argv) != 0) {
		exit(1);
	}

	/* Init exec environment */
        env_init(argc, argv);

	if (opt_lib_dir == NULL) {
		char *dir = env_devdir("lib");
		if (dir == NULL) {
			opt_lib_dir = LIB_DIR;
		}
		else {
			opt_lib_dir = dir;
			log_str("Running in development enviroment: lib directory = '%s'", dir);
		}
	}

	log_str("-----");
	if (opt_daemon) {
		run_as_daemon();
	}

	/* Init log management */
	log_init(NAME);
	log_str(options_summary);

        /* If a local tile is given in command line arguments, force off-line mode */
        if ((argc > 1) || (opt_tile != NULL)) {
                log_str("Using local tile(s): off-line mode forced");
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
        if ((argc > 1) || (opt_tile != NULL)) {
		int i;

		ctx_t *ctx = ctx_alloc();

                /* Start local tiles, if any */
                if (opt_tile != NULL) {
                        char *s = opt_tile;
                        while (s != NULL) {
                                char *sep = strchr(s, ',');
                                if (sep != NULL) {
                                        *(sep++) = '\0';
                                }
                                ctx_tile_feed_local(ctx, s);
                                s = sep;
                        }
                }

		for (i = 1; i < argc; i++) {
			char *path = argv[i];
			ctx_tile_feed_local(ctx, path);
		}

		engine_start(ctx);
		ctx_free(ctx);
        }
        else {
                /* Create lib directory */
                log_debug(1, "Library path = '%s'", opt_lib_dir);
                if (create_dir(opt_lib_dir, 0755) != 0) {
                        exit(2);
                }

                if (opt_api_key != NULL) {
                        /* Advertise device */
                        if (hello_request() < 0) {
                                log_str("INFO    : New HELLO attempt in %d seconds", HELLO_RETRY_INIT_DELAY);
                                sys_timeout(HELLO_RETRY_INIT_DELAY*1000, (sys_func_t) hello_request, NULL);
                        }
                }
                else {
                        /* Off-line mode: try to get tile from the cache */
                        ctx_t *ctx = hello_cache_load();
                        if (ctx == NULL) {
                                log_str("ERROR: No API-Key provided, No tile found in the cache: exiting.");
                                exit(2);
                        }
                }
        }

	sys_run();

	return 0;
}
