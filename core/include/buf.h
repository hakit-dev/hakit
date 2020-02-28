/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Memory allocator for buffers
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_BUF_H__
#define __HAKIT_BUF_H__

typedef struct {
	unsigned char *base;
	int size;
	int len;
} buf_t;

#define BUF_PTR(buf) ((buf).base + (buf).len)

extern void buf_init(buf_t *buf);
extern void buf_cleanup(buf_t *buf);
extern int buf_grow(buf_t *buf, int needed_size);

extern int buf_append(buf_t *buf, unsigned char *ptr, int len);
extern int buf_append_byte(buf_t *buf, unsigned char c);
extern int buf_append_str(buf_t *buf, char *str);
extern int buf_append_int(buf_t *buf, int i);
extern int buf_append_fmt(buf_t *buf, char *fmt, ...);
extern int buf_append_zero(buf_t *buf, int len);

extern int buf_set(buf_t *buf, unsigned char *ptr, int len);
extern int buf_set_str(buf_t *buf, char *str);
extern int buf_set_int(buf_t *buf, int v);

extern void buf_shift(buf_t *buf, int ofs);

#endif /* __HAKIT_BUF_H__ */
