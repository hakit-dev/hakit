#include <stdio.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/time.h>

#include "options.h"
#include "buf.h"
#include "log.h"


static char *log_prefix = "HaKit";
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
		openlog(log_prefix, LOG_PID, LOG_LOCAL0);
	}
}


static void log_flush(void)
{
	if (log_buf.len > 0) {
		if (log_buf.base[log_buf.len-1] == '\n') {
			fwrite(log_buf.base, 1, log_buf.len, log_f);
			fflush(log_f);

			log_buf.len--;
			log_buf.base[log_buf.len] = '\0';

			syslog(LOG_DEBUG, (char *) log_buf.base);

			log_buf.len = 0;
		}
	}
}


static void log_putc(char c)
{
	log_open();
	buf_append(&log_buf, (unsigned char *) &c, 1);
	log_flush();
}


static void log_vprintf(const char *fmt, va_list ap)
{
	char str[1024];
	int len;

	len = vsnprintf(str, sizeof(str), fmt, ap);

	if (len > 0) {
		log_open();
		buf_append(&log_buf, (unsigned char *) str, len);
		log_flush();
	}
}


void log_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_vprintf(fmt, ap);
	va_end(ap);
}


void log_tstamp(void)
{
	static struct timeval t0 = {0,0};
	struct timeval t;
	int dt_sec, dt_usec;

	if (t0.tv_sec == 0) {
		gettimeofday(&t0, NULL);
	}

	gettimeofday(&t, NULL);

	dt_sec = t.tv_sec - t0.tv_sec;
	dt_usec = t.tv_usec - t0.tv_usec;
	if (dt_usec < 0) {
		dt_usec += 1000000;
		dt_sec--;
	}

	log_printf("%d.%06d ", dt_sec, dt_usec);
}


void log_str(const char *fmt, ...)
{
	va_list ap;

	log_tstamp();

	va_start(ap, fmt);
	log_vprintf(fmt, ap);
	va_end(ap);

	log_putc('\n');
}


void log_debug(int level, const char *fmt, ...)
{
	va_list ap;

	if (opt_debug >= level) {
		log_tstamp();

		va_start(ap, fmt);
		log_vprintf(fmt, ap);
		va_end(ap);

		log_putc('\n');
	}
}


void log_hex(unsigned char *buf, int size)
{
	int i;

	log_putc('{');
	for (i = 0; i < size; i++) {
		log_printf(" %02X", buf[i]);
	}
	log_printf(" }");
}


void log_debug_data(unsigned char *buf, int size)
{
	int i;

	if (opt_debug >= 3) {
		log_printf("  = ");
		log_hex(buf, size);
		log_putc('\n');
	}


	if (opt_debug >= 2) {
		int crlf = 0;

		log_printf("  = \"");
		for (i = 0; i < size; i++) {
			unsigned char c = buf[i];

			if (crlf >= 4) {
				log_printf("...");
				break;
			}

			if ((c < ' ') || (c > 127)) {
				switch (c) {
				case '\n':
					log_printf("\\n");
					crlf++;
					break;
				case '\r':
					log_printf("\\r");
					crlf++;
					break;
				default:
					log_printf("\\x%02X", c);
					crlf = 0;
					break;
				}
			}
			else {
				log_putc(c);
				crlf = 0;
			}
		}
		log_putc('"');
		log_putc('\n');
	}
}
