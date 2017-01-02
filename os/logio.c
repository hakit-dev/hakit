/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2016 Sylvain Giroudon
 *
 * Log output management
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/time.h>

#include "buf.h"
#include "options.h"
#include "log.h"


#define LOG_MAX_SIZE (1024*1024)
#define LOG_MAX_BACKUP 4

#define LOG_DIRNAME "/var/log/"
#define LOG_BASENAME "hakit"
#define LOG_SUFFIX ".log"

static char *log_prefix = "HAKit";
static const char *log_filename = LOG_DIRNAME LOG_BASENAME LOG_SUFFIX;
static FILE *log_f = NULL;
static buf_t log_buf = {};


static void log_flush(void);


void log_init(char *prefix)
{
	log_prefix = prefix;
	buf_init(&log_buf);
}


static void log_open(void)
{
	static int error_shown = 0;

	/* Nothing to do if log file alreay open */
	if (log_f != NULL) {
		return;
	}

	/* Do not attempt to open log file if an error previously occured */
	if (error_shown) {
		return;
	}

	/* Open log file in daemon mode only */
	if (opt_daemon) {
		log_f = fopen(log_filename, "a");
		if (log_f == NULL) {
			error_shown = 1;
			log_str("WARNING: Cannot open log file '%s': %s\n", log_filename, strerror(errno));
		}
	}
}


static void log_close(void)
{
	log_flush();

	if (log_f != NULL) {
		fclose(log_f);
		log_f = NULL;
	}

	buf_cleanup(&log_buf);
}


static int log_filter(const struct dirent *ent)
{
	return (strncmp(ent->d_name, LOG_BASENAME "_", strlen(LOG_BASENAME "_")) == 0);
}


static void log_cleanup(void)
{
	struct dirent **ls;
	int n, i;

	n = scandir(LOG_DIRNAME, &ls, log_filter, alphasort);
	if (n > LOG_MAX_BACKUP) {
		for (i = 0; i < (n-LOG_MAX_BACKUP); i++) {
			struct dirent *ent = ls[i];
			char *name = ent->d_name;
			char path[strlen(LOG_DIRNAME)+strlen(name)+1];
			snprintf(path, sizeof(path), LOG_DIRNAME "%s", name);

			if (remove(path) == 0) {
				log_str("Old log file '%s' removed", path);
			}
			else {
				log_str("WARNING: Failed to remove backup log file '%s': %s", path, strerror(errno));
			}
		}
	}
}


static void log_rotate(void)
{
	static int error_shown = 0;
	struct timeval t;
	char path[strlen(LOG_DIRNAME)+strlen(LOG_BASENAME)+strlen(LOG_SUFFIX)+16];

	/* Do not attempt log-rotate if an error previously occured */
	if (error_shown) {
		return;
	}

	log_close();

	gettimeofday(&t, NULL);
	strftime(path, sizeof(path), LOG_DIRNAME LOG_BASENAME "_%y%m%dT%H%M%S" LOG_SUFFIX, localtime(&t.tv_sec));

	if (rename(log_filename, path) == 0) {
		log_str("Previous log file: '%s'", path);
		log_cleanup();
	}
	else {
		log_str("WARNING: Failed to rename log file '%s' to '%s': %s", log_filename, path, strerror(errno));
	}
}


static void log_flush(void)
{
	if (log_buf.len > 0) {
		if (log_buf.base[log_buf.len-1] == '\n') {
			FILE *f = (log_f != NULL) ? log_f : stderr;
			fwrite(log_buf.base, 1, log_buf.len, f);
			fflush(f);
			log_buf.len = 0;

			/* Perform a log rotate if file is too large */
			if (log_f != NULL) {
				if (ftell(log_f) >= LOG_MAX_SIZE) {
					log_rotate();
				}
			}
		}
	}
}


void log_put(char *str, int len)
{
	log_open();
	buf_append(&log_buf, (unsigned char *) str, len);
	log_flush();
}
