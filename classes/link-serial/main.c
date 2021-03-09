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
#include "str_argv.h"

#include "version.h"

#define CLASS_NAME "link-serial"

#define DEFAULT_DEV  "/dev/ttyUSB0"
#define DEFAULT_SPEED 115200

#define RETRY_DELAY 5000


typedef struct {
	char *str;
	int len;
} prefix_t;

typedef struct {
	hk_obj_t *obj;
	prefix_t tx_prefix;
	prefix_t rx_prefix;
	hk_pad_t *connected_pad;
	hk_tab_t inputs;
	hk_tab_t outputs;
	char *tty_name;
	int tty_speed;
	io_channel_t tty_chan;
	sys_tag_t timeout;
	buf_t lbuf;
} ctx_t;


static int tty_retry(ctx_t *ctx);


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


static void tty_recv_line(ctx_t *ctx, char *str)
{
	int argc;
	char **argv = NULL;
	int i;

	log_debug(2, "%s [RECV]: '%s'", ctx->tty_name, str);

	if (str == NULL) {
		return;
	}

	/* Filter against RX prefix */
	if (ctx->rx_prefix.str != NULL) {
		if (strncmp(str, ctx->rx_prefix.str, ctx->rx_prefix.len) != 0) {
			return;
		}
		str += ctx->rx_prefix.len;
	}

	/* Extract key/value pairs from device event */
	argc = str_argv(str, &argv);

	for (i = 0; i < argc; i++) {
		char *key = argv[i];
		char *value = key;
		hk_pad_t *pad;

		while ((*value > ' ') && (*value != '=')) {
			value++;
		}
		if (*value == '=') {
			*(value++) = '\0';
		}

		log_debug(2, "%s [RECV]:   => %s='%s'", ctx->tty_name, key, value);

		/* No value provided => do nothing */
		if (*value != '\0') {
			/* Find output pad corresponding to the key */
			pad = find_pad(&ctx->outputs, key);
			if (pad != NULL) {
				/* Update output pad */
				hk_pad_update_str(pad, value);
			}
			else {
				log_str("%s [WARN]: Received update request for unknown output pad: %s", ctx->tty_name, key);
			}
		}
	}
}


static void tty_recv(ctx_t *ctx, char *buf, int size)
{
	int i;

	/* Stop object is TTY hangup detected */
	if (buf == NULL) {
		log_str("%s: Serial device hung up.", ctx->tty_name);
		io_channel_close(&ctx->tty_chan);
		ctx->timeout = sys_timeout(RETRY_DELAY, (sys_func_t) tty_retry, ctx);

		/* Update connection state pad */
		hk_pad_update_int(ctx->connected_pad, 0);

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
	int len = 0;
	va_list ap;

	if (ctx->tx_prefix.str != NULL) {
		len += snprintf(str+len, sizeof(str)-1-len, "%s", ctx->tx_prefix.str);
	}

	va_start(ap, fmt);
	len += vsnprintf(str+len, sizeof(str)-1-len, fmt, ap);
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

	/* Get serial device name */
	ctx->tty_name = hk_prop_get(&obj->props, "tty");
	if (ctx->tty_name == NULL) {
		ctx->tty_name = DEFAULT_DEV;
	}

	/* Get serial speed */
	ctx->tty_speed = hk_prop_get_int(&obj->props, "speed");
	if (ctx->tty_speed <= 0) {
		ctx->tty_speed = DEFAULT_SPEED;
	}

	/* Get tx/rx command prefixes */
	ctx->tx_prefix.str = hk_prop_get(&obj->props, "tx-prefix");
	if (ctx->tx_prefix.str != NULL) {
		log_debug(1, "%s: tx-prefix='%s'", ctx->tty_name, ctx->tx_prefix.str);
		ctx->tx_prefix.len = strlen(ctx->tx_prefix.str);
	}

	ctx->rx_prefix.str = hk_prop_get(&obj->props, "rx-prefix");
	if (ctx->rx_prefix.str != NULL) {
		log_debug(1, "%s: rx-prefix='%s'", ctx->tty_name, ctx->rx_prefix.str);
		ctx->rx_prefix.len = strlen(ctx->rx_prefix.str);
	}

	/* Clear io channel */
	io_channel_clear(&ctx->tty_chan);

	/* Update connection state pad */
	ctx->connected_pad = hk_pad_create(obj, HK_PAD_OUT, "connected");
	hk_pad_update_int(ctx->connected_pad, 0);

	return 0;
}


static int tty_connect(ctx_t *ctx)
{
	int fd;

	/* Open serial device */
        fd = serial_open(ctx->tty_name, ctx->tty_speed, 0);

	/* Abort if serial device open fails */
	if (fd < 0) {
		return -1;
	}

	/* Hook TTY to io channel */
	io_channel_setup(&ctx->tty_chan, fd, (io_func_t) tty_recv, ctx);

	/* Update connection state pad */
	hk_pad_update_int(ctx->connected_pad, 1);

	/* Ask device to send status */
	//tty_send(ctx, "status");

	return 0;
}


static int tty_retry(ctx_t *ctx)
{
	if (tty_connect(ctx)) {
		return 1;
	}

	log_str("%s: Serial device is back...", ctx->tty_name);

	ctx->timeout = 0;

	return 0;
}


static void _start(hk_obj_t *obj)
{
	ctx_t *ctx = obj->ctx;

	if (tty_connect(ctx)) {
		ctx->timeout = sys_timeout(RETRY_DELAY, (sys_func_t) tty_retry, ctx);
	}
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;
	tty_send(ctx, "%s %s", pad->name, value);
}


const hk_class_t _class_link_serial = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
