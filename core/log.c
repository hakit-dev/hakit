/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Info and Debug log management
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
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
	struct timeval t;
	char str[32];
	int len;

	gettimeofday(&t, NULL);

	len = strftime(str, sizeof(str), "[%Y-%m-%d %H:%M:%S", localtime(&t.tv_sec));
	len += snprintf(str+len, sizeof(str)-len, ".%03ld] ", t.tv_usec/1000);
	log_put(str, len);
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
