#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>

#include "options.h"
#include "log.h"


static void log_putc(char c)
{
	log_put(&c, 1);
}


static void log_vprintf(const char *fmt, va_list ap)
{
	char str[1024];
	int len;

	len = vsnprintf(str, sizeof(str), fmt, ap);

	if (len > 0) {
		log_put(str, len);
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
	char str[32];

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

	strftime(str, sizeof(str), "%d-%b-%Y %H:%M:%S", localtime(&t.tv_sec));

	log_printf("%s %d.%06d ", str, dt_sec, dt_usec);
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


void log_data_hex(unsigned char *buf, int size)
{
	int i;

	log_putc('{');
	for (i = 0; i < size; i++) {
		log_printf(" %02X", buf[i]);
	}
	log_printf(" }");
}


void log_data_str(unsigned char *buf, int size)
{
	int crlf = 0;
	int i;

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
}


void log_debug_data(unsigned char *buf, int size)
{
	if (opt_debug >= 4) {
		log_printf("  = ");
		log_data_hex(buf, size);
		log_putc('\n');
	}


	if (opt_debug >= 3) {
		log_printf("  = \"");
		log_data_str(buf, size);
		log_putc('"');
		log_putc('\n');
	}
}
