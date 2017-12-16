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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>

#include "types.h"
#include "buf.h"


#define CHUNK_SIZE 256


void buf_init(buf_t *buf)
{
	buf->base = NULL;
	buf->size = 0;
	buf->len = 0;
}

void buf_cleanup(buf_t *buf)
{
	if (buf->base != NULL) {
		memset(buf->base, 0, buf->size);
		free(buf->base);
	}

	buf_init(buf);
}


int buf_grow(buf_t *buf, int needed_size)
{
	needed_size++;   // Make room for putting extra NUL character

	if (buf->size < (buf->len + needed_size)) {
		int size0 = buf->size;
		buf->size = CEIL_DIV(buf->len + needed_size, CHUNK_SIZE) * CHUNK_SIZE;
		buf->base = realloc(buf->base, buf->size);
		if (buf->base == NULL) {
			return -1;
		}

		memset(buf->base+size0, 0, buf->size-size0);
	}

	return 0;
}


int buf_append(buf_t *buf, unsigned char *ptr, int len)
{
	int ret = buf_grow(buf, len);

	if (ret == 0) {
		memcpy(buf->base+buf->len, ptr, len);
		buf->len += len;
		buf->base[buf->len] = '\0';  // Make buffer NUL-terminated-string-safe
	}

	return ret;
}


int buf_append_byte(buf_t *buf, unsigned char c)
{
	return buf_append(buf, &c, 1);
}


int buf_append_str(buf_t *buf, char *str)
{
	return buf_append(buf, (unsigned char *) str, strlen(str));
}


int buf_append_int(buf_t *buf, int i)
{
	char str[16];
	int len = snprintf(str, sizeof(str), "%d", i);
	return buf_append(buf, (unsigned char *) str, len);
}


int buf_append_fmt(buf_t *buf, char *fmt, ...)
{
	va_list ap;
	char str[1024];
	int len;

	va_start(ap, fmt);
	len = vsnprintf(str, sizeof(str), fmt, ap);
	va_end(ap);

	return buf_append(buf, (unsigned char *) str, len);
}


int buf_append_zero(buf_t *buf, int len)
{
	int ret = buf_grow(buf, len);
	buf->len += len;
	return ret;
}


int buf_set(buf_t *buf, unsigned char *ptr, int len)
{
	int ret = buf_grow(buf, len);

	if (ret == 0) {
		memcpy(buf->base, ptr, len);
		buf->len = len;
		buf->base[buf->len] = '\0';  // Make buffer NUL-terminated-string-safe
	}

	return ret;
}


int buf_set_str(buf_t *buf, char *str)
{
	return buf_set(buf, (unsigned char *) str, strlen(str));
}


int buf_set_int(buf_t *buf, int v)
{
	char str[32];
	int len = snprintf(str, sizeof(str), "%d", v);
	return buf_set(buf, (unsigned char *) str, len);
}


void buf_shift(buf_t *buf, int ofs)
{
        if (ofs > 0) {
                if (ofs < buf->len) {
                        int i;

                        for (i = ofs; i < buf->len; i++) {
                                buf->base[i-ofs] = buf->base[i];
                        }

                        buf->len -= ofs;
                }
                else {
                        buf->len = 0;
                }
        }
}
