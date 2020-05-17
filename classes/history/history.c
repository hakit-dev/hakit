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
#include <malloc.h>
#include <time.h>
#include <errno.h>

#include "types.h"
#include "log.h"
#include "buf.h"
#include "sys.h"
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

#define BUCKET_MAXSIZE 10000
#define BUCKET_FLUSH_TIMEOUT 10000


static const char *history_filename_prefix = "/tmp/hakit-history";


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


static void history_bucket_start(history_t *h)
{
	bucket_t *bucket = &h->buckets[h->ibucket];
	int size;

	h->t = time(NULL);
	h->current_id = -1;
	log_debug(2, "history_bucket_start t0=%lld", h->t);

	bucket->t0 = h->t;
	bucket->buf.len = 0;

	if (bucket->fname != NULL) {
		free(bucket->fname);
	}

	size = strlen(history_filename_prefix) + 12;
	bucket->fname = malloc(size);
	snprintf(bucket->fname, size, "%s-%010llx", history_filename_prefix, bucket->t0);

	bucket->fpos = 0;

	history_append_value(&bucket->buf, 0x02, h->t);
}


static void history_bucket_flush(history_t *h)
{
	bucket_t *bucket = &h->buckets[h->ibucket];
	int len = bucket->buf.len - bucket->fpos;
	FILE *f;

	if (len > 0) {
		log_debug(2, "history_bucket_flush '%s': %d bytes", bucket->fname, len);

		f = fopen(bucket->fname, "a");
		if (f != NULL) {
			/* Write signal declaration if we start a new history file */
			if (bucket->fpos == 0) {
				if (fwrite(h->hdr.base, 1, h->hdr.len, f) < 0) {
					log_str("ERROR: Cannot write history file '%s': %s", bucket->fname, strerror(errno));
				}
			}

			if (fwrite(bucket->buf.base+bucket->fpos, 1, len, f) < 0) {
				log_str("ERROR: Cannot write history file '%s': %s", bucket->fname, strerror(errno));
			}
			else {
				bucket->fpos = bucket->buf.len;
			}

			fclose(f);
		}
		else {
			log_str("ERROR: Cannot open history file '%s': %s", bucket->fname, strerror(errno));
		}
	}
}


static int history_bucket_flush_timeout(history_t *h)
{
	h->timeout_tag = 0;
	history_bucket_flush(h);
	return 0;
}


void history_signal_declare(history_t *h, int id, char *name)
{
	/* Dump new signal to history log */
	history_append_value(&h->hdr, 0x00, id);
	buf_append(&h->hdr, (unsigned char *) name, strlen(name)+1);
	log_debug(2, "history_signal_declare %d '%s'", id, name);

	h->current_id = id;
}


void history_init(history_t *h)
{
	int i;

	buf_init(&h->hdr);

	for (i = 0; i < NBUCKETS; i++) {
		bucket_t *bucket = &h->buckets[i];
		bucket->t0 = 0;
		buf_init(&bucket->buf);
	}

	h->ibucket = 0;
	h->timeout_tag = 0;

	history_bucket_start(h);

	sys_quit_handler((sys_func_t) history_bucket_flush_timeout, h);
}


static void history_select(history_t *h, int id)
{
	bucket_t *bucket = &h->buckets[h->ibucket];
	time_t t = time(NULL);

	if (id != h->current_id) {
		h->current_id = id;
		history_append_value(&bucket->buf, 0x01, id);
	}

	if (t != h->t) {
		time_t dt = t - h->t;
		h->t = t;
		if (dt < 64) {
			buf_append_byte(&bucket->buf, 0xC0 | dt);
		}
		else {
			history_append_value(&bucket->buf, 0x03, dt);
		}
	}
}


static void history_feed_str(history_t *h, char *str)
{
	bucket_t *bucket = &h->buckets[h->ibucket];

	buf_append_byte(&bucket->buf, 0x05);
	buf_append(&bucket->buf, (unsigned char *) str, strlen(str)+1);
}


static void history_feed_int(history_t *h, long long value)
{
	bucket_t *bucket = &h->buckets[h->ibucket];

	if ((value >= -32) && (value < 31)) {
		buf_append_byte(&bucket->buf, 0x80 | value);
	}
	else {
		history_append_value(&bucket->buf, 0x04, value);
	}
}


void history_feed(history_t *h, int id, char *value)
{
	char *s = value;

	if (h->timeout_tag) {
		sys_remove(h->timeout_tag);
		h->timeout_tag = 0;
	}

	history_select(h, id);

	while ((*s >= '0') && (*s <= '9')) {
		s++;
	}

	if (*s == '\0') {
		history_feed_int(h, strtoll(value, NULL, 10));
	}
	else {
		history_feed_str(h, value);
	}

	if (history.buckets[h->ibucket].buf.len >= BUCKET_MAXSIZE) {
		history_bucket_flush(h);

		h->ibucket++;
		if (h->ibucket >= NBUCKETS) {
			h->ibucket = 0;
		}
		history_bucket_start(h);
	}
	else {
		h->timeout_tag = sys_timeout(BUCKET_FLUSH_TIMEOUT, (sys_func_t) history_bucket_flush_timeout, h);
	}
}
