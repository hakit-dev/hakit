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

#ifndef __HAKIT_HISTORY_H__
#define __HAKIT_HISTORY_H__

#include "buf.h"

#define NBUCKETS 10

typedef struct {
	long long t0;
	buf_t buf;
	char *fname;
	int fpos;
} bucket_t;

typedef struct {
	buf_t hdr;
	bucket_t buckets[NBUCKETS];
	int ibucket;
	long long t;
	int current_id;
	sys_tag_t timeout_tag;
} history_t;

extern void history_init(history_t *h);
extern void history_signal_declare(history_t *h, int id, char *name);

extern void history_feed(history_t *h, int id, char *value);

#endif /* __HAKIT_HISTORY_H__ */
