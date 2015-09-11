/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>

#include "types.h"
#include "log.h"
#include "mod.h"
#include "serial.h"
#include "buf.h"
#include "io.h"

#include "version.h"

#define CLASS_NAME "link-serial"

#define DEFAULT_DEV  "/dev/ttyUSB0"
#define DEFAULT_SPEED 115200


typedef struct {
	hk_obj_t *obj;
	hk_tab_t inputs;
	hk_tab_t outputs;
	char *tty_name;
	io_channel_t tty_chan;
	buf_t lbuf;
} ctx_t;


static hk_pad_t *find_pad(hk_tab_t *tab, char *key)
{
	int i;

	for (i = 0; i < tab->nmemb; i++) {
		hk_pad_t *pad = HK_TAB_VALUE(*tab, hk_pad_t *, i);
		if (strcmp(pad->name, key) == 0) {
			return pad;
		}
	}

	return NULL;
}


static void tty_recv_line(ctx_t *ctx, char *key)
{
	char *value;
	hk_pad_t *pad;

	log_debug(2, "%s [RECV]: '%s'", ctx->tty_name, key);

	/* Extract key and value from device event */
	value = strchr(key, '=');
	if (value == NULL) {
		log_str("%s [WARN]: Received badly formatted datagram: %s", ctx->tty_name, key);
		return;
	}

	*(value++) = '\0';

	/* Find output pad corresponding to the key */
	pad = find_pad(&ctx->outputs, key);
	if (pad == NULL) {
		log_str("%s [WARN]: Received update request for unknown output pad: %s", ctx->tty_name, key);
		return;
	}

	/* Update output pad */
	hk_pad_update_str(pad, value);
}


static void tty_recv(ctx_t *ctx, char *buf, int size)
{
	int i;

	/* Stop object is TTY hangup detected */
	if (buf == NULL) {
		log_str("Serial device hangup");
		return;
	}

	for (i = 0; i < size; i++) {
		unsigned char c = buf[i] & 0x7F;
		if (c == '\n') {
			tty_recv_line(ctx, (char *) ctx->lbuf.base);
			ctx->lbuf.len = 0;
		}
		else if (c >= ' ') {
			buf_append_byte(&ctx->lbuf, c);
		}
	}
}


static int tty_send(ctx_t *ctx, char *fmt, ...)
{
	char str[128];
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(str, sizeof(str)-1, fmt, ap);
	va_end(ap);

	log_debug(2, "%s [SEND]: '%s'", ctx->tty_name, str);

	str[len++] = '\n';

	return io_channel_write(&ctx->tty_chan, str, len);
}



static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *s1, *s2;
	hk_pad_t **ppad;

	/* Create object context */
	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	/* Create input pads */
	hk_tab_init(&ctx->inputs, sizeof(hk_pad_t *));
	s1 = hk_prop_get(&obj->props, "inputs");
	while (s1 != NULL) {
		s2 = strchr(s1, ',');
		if (s2 != NULL) {
			*(s2++) = '\0';
		}

		ppad = hk_tab_push(&ctx->inputs);
		*ppad = hk_pad_create(obj, HK_PAD_IN, s1);

		s1 = s2;
	}

	/* Create output pads */
	hk_tab_init(&ctx->outputs, sizeof(hk_pad_t *));
	s1 = hk_prop_get(&obj->props, "outputs");
	while (s1 != NULL) {
		s2 = strchr(s1, ',');
		if (s2 != NULL) {
			*(s2++) = '\0';
		}

		ppad = hk_tab_push(&ctx->outputs);
		*ppad = hk_pad_create(obj, HK_PAD_OUT, s1);

		s1 = s2;
	}

	return 0;
}


static void _start(hk_obj_t *obj)
{
	ctx_t *ctx = obj->ctx;
	int speed;
	int fd;

	/* Get serial device name */
	ctx->tty_name = hk_prop_get(&obj->props, "tty");
	if (ctx->tty_name == NULL) {
		ctx->tty_name = DEFAULT_DEV;
	}

	/* Get serial speed */
	speed = hk_prop_get_int(&obj->props, "speed");
	if (speed <= 0) {
		speed = DEFAULT_SPEED;
	}

	/* Open serial device */
        fd = serial_open(ctx->tty_name, speed, 0);

	/* Abort if serial device open fails */
	if (fd < 0) {
		return;
	}

	/* Hook TTY to io channel */
	io_channel_setup(&ctx->tty_chan, fd, (io_func_t) tty_recv, ctx);

	/* Ask device to send status */
	tty_send(ctx, "status");
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	tty_send(ctx, "%s=%s", pad->name, value);
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
