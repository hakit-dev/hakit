/*
 * HAKit - The Home Automation Kit - www.hakit.net
 * Copyright (C) 2014-2015 Sylvain Giroudon
 *
 * Signal history logging
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "types.h"
#include "log.h"
#include "buf.h"
#include "history.h"


// History log entry format:
//   OP-CODE    DESCRIPTION              DATA FOLLOWING
//   ---------  ----------------------   -----------------------------------------------------------------------------
//   0sss 0000  Declare signal           Signal id (sss+1 bytes, big-endian), followed by nul-terminated signal name.
//   0sss 0001  Select signal            Signal id (sss+1 bytes, big-endian)
//   0sss 0010  Set absolute time stamp  Absolute time stamp (sss+1 bytes, big endian)
//   0sss 0011  Set relative time stamp  Seconds relative to latest absolute time stamp (sss+1 bytes, big endian)
//   0sss 0100  Log long value           Signal value (sss+1 bytes, big endian, sign-extended)
//   0000 0101  Log string value         Signal value as a nul-terminated string
//   10vv vvvv  Log short value          No data. vvvvvv = -32..31
//   11vv vvvv  Set relative time stamp  No data. vvvvvv = seconds relative to latest absolute time stamp

#define BUCKET_MAXSIZE 100
#define NBUCKETS 10

typedef struct {
	buf_t hdr;
	buf_t bufs[NBUCKETS];
	int ibuf;
	time_t t0;
	int current;
} history_t;

static history_t history;


static void history_append_value(buf_t *buf, unsigned char op, long long value)
{
	int i;

	for (i = 0; i < sizeof(value); i++) {
		int len = i+1;
		long long lim = 1ULL << ((len*8)-1);

		if ((value >= -lim) && (value < lim)) {
			int j;

			buf_append_byte(buf, op | (i << 4));
			for (j = 0; j < len; j++) {
				unsigned char c = value >> (8*(len-j-1));
				buf_append_byte(buf, c);
			}

			break;
		}
	}
}


static void history_start_bucket(history_t *h)
{
	h->bufs[h->ibuf].len = 0;
	h->t0 = time(NULL);
	history_append_value(&h->bufs[h->ibuf], 0x02, h->t0);
	h->current = -1;
	log_debug(2, "history_start_bucket t0=%lld", (long long) h->t0);
}


void history_signal_declare(int id, char *name)
{
	/* Dump new signal to history log */
	history_append_value(&history.hdr, 0x00, id);
	buf_append(&history.hdr, (unsigned char *) name, strlen(name)+1);
	log_debug(2, "history_signal_declare %d '%s'", id, name);

	history.current = id;
}


void history_init(void)
{
	int i;

	buf_init(&history.hdr);

	for (i = 0; i < NBUCKETS; i++) {
		buf_init(&history.bufs[i]);
	}

	history.ibuf = 0;

	history_start_bucket(&history);
}


static void history_select(int id)
{
	buf_t *buf = &history.bufs[history.ibuf];
	time_t t = time(NULL);

	if (id != history.current) {
		history.current = id;
		history_append_value(buf, 0x01, id);
	}

	if (t != history.t0) {
		time_t dt = t - history.t0;
		history.t0 = t;
		if (dt < 64) {
			buf_append_byte(buf, 0xC0 | dt);
		}
		else {
			history_append_value(buf, 0x03, dt);
		}
	}
}


static void history_feed_str(int id, char *str)
{
	buf_t *buf = &history.bufs[history.ibuf];
	buf_append_byte(buf, 0x05);
	buf_append(buf, (unsigned char *) str, strlen(str)+1);
}


static void history_feed_int(int id, long long value)
{
	buf_t *buf = &history.bufs[history.ibuf];

	if ((value >= -32) && (value < 31)) {
		buf_append_byte(buf, 0x80 | value);
	}
	else {
		history_append_value(buf, 0x04, value);
	}
}


void history_feed(int id, char *value)
{
	char *s = value;

	history_select(id);

	while ((*s >= '0') && (*s <= '9')) {
		s++;
	}

	if (*s == '\0') {
		history_feed_int(id, strtoll(value, NULL, 10));
	}
	else {
		history_feed_str(id, value);
	}

	if (history.bufs[history.ibuf].len >= BUCKET_MAXSIZE) {
		history.ibuf++;
		if (history.ibuf >= NBUCKETS) {
			history.ibuf = 0;
		}
		history_start_bucket(&history);
	}
}


static long long history_read_value(unsigned char *buf, int len)
{
	long long value = (buf[0] & 0x80) ? -1:0;
	int i;

	for (i = 0; i < len; i++) {
		int sft = 8 * (len-i-1);
		unsigned long long mask0 = 0xFFULL << sft;
		unsigned long long mask = (((unsigned long long) buf[i]) << sft);
		value = (value & ~mask0) | mask;
	}

	return value;
}


static int history_find_first_bucket(history_t *h)
{
	long long tstamp = 0;
	int ifirst = 0;
	int ibuf;

	for (ibuf = 0; ibuf < NBUCKETS; ibuf++) {
		buf_t *buf = &h->bufs[ibuf];

		if (buf->len > 1) {
			unsigned char op = buf->base[0];

			/* Get absolute timestamp at the begining of the buf */
			if ((op & 0xCF) == 0x02) {
				int len = (op >> 4) + 1;
				long long t = history_read_value(buf->base+1, len);
				if ((tstamp == 0) || (t < tstamp)) {
					tstamp = t;
					ifirst = ibuf;
				}
			}
		}
	}

	return ifirst;
}


void history_dump(FILE *f)
{
	int id = 0;
	long long tstamp = 0;
	buf_t *bufs[NBUCKETS+1] = {&history.hdr};
	int ibuf, i;

	/* Set ordered list of buffers */
	i = history_find_first_bucket(&history);
	for (ibuf = 0; ibuf < NBUCKETS; ibuf++) {
		bufs[ibuf+1] = &history.bufs[i];
		i++;
		if (i >= NBUCKETS) {
			i = 0;
		}
	}

	fprintf(f, "# History buffers:\n");
	for (ibuf = 0; ibuf < ARRAY_SIZE(bufs); ibuf++) {
		buf_t *buf = bufs[ibuf];

		if (buf->len > 0) {
			int i;

			fprintf(f, "# [%d]", buf->len);

			for (i = 0; i < buf->len; i++) {
				fprintf(f, " %02X", buf->base[i]);
			}

			fprintf(f, "\n");
		}
	}

	for (ibuf = 0; ibuf < ARRAY_SIZE(bufs); ibuf++) {
		buf_t *buf = bufs[ibuf];
		int i = 0;

		while (i < buf->len) {
			unsigned char op = buf->base[i++];

			if (op & 0x80) {
				unsigned char v = op & 0x3F;

				if (op & 0x40) {  // Set relative time stamp
					tstamp += v;
				}
				else {  // Log short value
					fprintf(f, "%8lld (%d) = %u\n", tstamp, id, v);
				}
			}
			else {
				int len = (op >> 4) + 1;
				char *str;
				long long dt;

				switch (op & 0x0F) {
				case 0x00:  // Declare signal
					id = history_read_value(buf->base+i, len);
					i += len;
					str = (char *) &buf->base[i];
					i += (strlen(str) + 1);
					fprintf(f, "# Declare signal '%s' as %d\n", str, id);
					break;
				case 0x01:  // Select signal
					id = history_read_value(buf->base+i, len);
					i += len;
					//fprintf(f, "# Select signal %d\n", id);
					break;
				case 0x02:  // Set absolute time stamp
					tstamp = history_read_value(buf->base+i, len);
					i += len;
					fprintf(f, "# Absolute time stamp: %lld\n", tstamp);
					break;
				case 0x03:  // Set relative time stamp
					dt = history_read_value(buf->base+i, len);
					tstamp += dt;
					i += len;
					fprintf(f, "# Relative time stamp: +%lld => %lld\n", dt, tstamp);
					break;
				case 0x04:  // Log long value
					fprintf(f, "%8lld (%d) = %lld\n", tstamp, id, history_read_value(buf->base+i, len));
					i += len;
					break;
				case 0x05:  // Log string value
					str = (char *) &buf->base[i];
					i += (strlen(str) + 1);
					fprintf(f, "%8lld (%d) = \"%s\"\n", tstamp, id, str);
					break;
				default:
					break;
				}
			}
		}
	}
}
