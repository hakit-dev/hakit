#include <stdio.h>
#include <syslog.h>

#include "buf.h"
#include "options.h"
#include "log.h"


static char *log_prefix = "HAKit";
static FILE *log_f = NULL;
static buf_t log_buf;


void log_init(char *prefix)
{
	log_prefix = prefix;
}


static void log_open(void)
{
	if (log_f == NULL) {
		log_f = stderr;
		buf_init(&log_buf);
		if (opt_daemon) {
			openlog(log_prefix, LOG_PID, LOG_LOCAL0);
		}
	}
}


static void log_flush(void)
{
	if (log_buf.len > 0) {
		if (log_buf.base[log_buf.len-1] == '\n') {
			fwrite(log_buf.base, 1, log_buf.len, log_f);
			fflush(log_f);

			if (opt_daemon) {
				log_buf.len--;
				log_buf.base[log_buf.len] = '\0';
				syslog(LOG_DEBUG, "%s", (char *) log_buf.base);
			}

			log_buf.len = 0;
		}
	}
}


void log_put(char *str, int len)
{
	log_open();
	buf_append(&log_buf, (unsigned char *) str, len);
	log_flush();
}
