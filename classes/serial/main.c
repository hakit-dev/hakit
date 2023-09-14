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
#include <pthread.h>
#include <errno.h>
#include <mqueue.h>

#include "types.h"
#include "log.h"
#include "mod.h"
#include "serial.h"
#include "buf.h"
#include "io.h"
#include "str_argv.h"

#include "version.h"

#define CLASS_NAME "serial"

#define DEFAULT_DEV  "/dev/ttyUSB0"
#define DEFAULT_SPEED 115200

#define RETRY_DELAY 5000


typedef struct {
	hk_obj_t *obj;
	hk_pad_t *connected;
	hk_pad_t *txd;
	hk_pad_t *rxd;
	hk_pad_t *rts;
	hk_pad_t *dtr;
	hk_pad_t *dsr;
	hk_pad_t *cts;
	hk_pad_t *cd;
	hk_pad_t *ri;
	char *tty_name;
	int tty_speed;
	io_channel_t tty_chan;
	sys_tag_t timeout;
	buf_t lbuf;
        pthread_t thr;
        int thr_ok;
	mqd_t thr_mq;
	sys_tag_t thr_mq_tag;
        int io_only;
} ctx_t;


static int tty_retry(ctx_t *ctx);


static void tty_hangup(ctx_t *ctx)
{
        log_str("%s: Serial device hung up.", ctx->tty_name);

        /* Cancel MODEM input watch thread */
        if (ctx->thr_ok) {
                if (pthread_cancel(ctx->thr) != 0) {
                        log_str("%s: Thread cancel failed: %s", ctx->tty_name, strerror(errno));
                }                        
                pthread_join(ctx->thr, NULL);
                log_str("%s: Thread canceled", ctx->tty_name);
                ctx->thr_ok = 0;
        }

        io_channel_close(&ctx->tty_chan);

        /* Update connection state pad */
        hk_pad_update_int(ctx->connected, 0);

        /* Start connect watch timer */
        ctx->timeout = sys_timeout(RETRY_DELAY, (sys_func_t) tty_retry, ctx);
}


static void tty_recv_line(ctx_t *ctx, char *str)
{
	if (str == NULL) {
		return;
	}

	log_debug(2, "%s [RECV]: '%s'", ctx->tty_name, str);

        hk_pad_update_str(ctx->rxd, str);
}


static void tty_recv(ctx_t *ctx, char *buf, int size)
{
	int i;

	/* Stop object is TTY hangup detected */
	if (buf == NULL) {
                tty_hangup(ctx);
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


static int tty_send(ctx_t *ctx, char *str)
{
	log_debug(2, "%s [SEND]: '%s'", ctx->tty_name, str);

	int ret = io_channel_write(&ctx->tty_chan, str, strlen(str));
        if (ret > 0) {
                io_channel_write(&ctx->tty_chan, "\r", 1);
        }

        return ret;
}


static void tty_set_modem_outputs(ctx_t *ctx, int flags)
{
        hk_pad_update_int(ctx->dsr, (flags & SERIAL_DSR) ? 1:0);
        hk_pad_update_int(ctx->cts, (flags & SERIAL_CTS) ? 1:0);
        hk_pad_update_int(ctx->cd, (flags & SERIAL_CD) ? 1:0);
        hk_pad_update_int(ctx->ri, (flags & SERIAL_RI) ? 1:0);
}


static void *tty_thread(void *arg)
{
        ctx_t *ctx = arg;

        log_str("%s: Thread started", ctx->obj->name);

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

        int flags = 0;
        while (flags >= 0) {
                flags = serial_modem_wait(ctx->tty_chan.fd);

                char mbuf = flags & 0xFF;
                if (mq_send(ctx->thr_mq, &mbuf, sizeof(mbuf), 0) < 0) {
                        log_str("PANIC: %s: Cannot send message: %s", ctx->obj->name, strerror(errno));
                        break;
                }
        }

        ctx->thr_ok = 0;

        log_str("%s: Thread terminated", ctx->obj->name);

        return NULL;
}


static int tty_thread_recv(ctx_t *ctx, int fd)
{
	char mbuf;
	ssize_t msize = mq_receive(ctx->thr_mq, &mbuf, sizeof(mbuf), NULL);
	if (msize < 0) {
		if ((errno != EAGAIN) && (errno != EINTR)) {
			log_str("PANIC: %s: Cannot handle thread message: %s", ctx->obj->name, strerror(errno));
		}
		return 0;
	}

	if (msize != 1) {
		log_str("PANIC: %s: Received thread message with illegal size (%d)", ctx->obj->name, msize);
		return 0;
	}

        /* Hangup signal */
        if (mbuf == 0xFF) {
                log_str("PANIC: %s: Watch thread i/o error", ctx->tty_name);

                /* Trigger hangup procedure if in i/o mode only */
                if (ctx->io_only) {
                        tty_hangup(ctx);
                }
                return 0;
        }

        /* Normal data */
        tty_set_modem_outputs(ctx, mbuf);

	return 1;
}


static int tty_thread_init(ctx_t *ctx)
{
        /* Only one init required */
        if (ctx->thr_mq_tag > 0) {
                return 0;
        }

        /* Create private messaging queue */
	char id[64];
        snprintf(id, sizeof(id), "/hakit-%s", ctx->obj->name);

        int flags = O_RDWR | O_CREAT | O_EXCL | O_NONBLOCK;

        struct mq_attr attr = {
                .mq_flags   = O_NONBLOCK,
                .mq_maxmsg  = 8,
                .mq_msgsize = 1,
        };

        ctx->thr_mq = mq_open(id, flags, 0600, &attr);
        if (ctx->thr_mq == -1) {
                log_str("ERROR: %s: Cannot create message queue: %s", ctx->obj->name, strerror(errno));
                return -1;
        }

        mq_unlink(id);  // Hide message queue from other processes

        /* Message queue receive callback */
        ctx->thr_mq_tag = sys_io_watch(ctx->thr_mq, (sys_io_func_t) tty_thread_recv, ctx);

        log_debug(1, "%s: Message queue initialized", ctx->obj->name);

        return 0;
}


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;

	/* Create object context */
	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

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

	/* Clear io channel */
	io_channel_clear(&ctx->tty_chan);

	/* Create input pads */
        ctx->txd = hk_pad_create(obj, HK_PAD_IN, "txd");
        ctx->rts = hk_pad_create(obj, HK_PAD_IN, "rts");
        ctx->dtr = hk_pad_create(obj, HK_PAD_IN, "dtr");

	/* Create output pads */
	ctx->rxd = hk_pad_create(obj, HK_PAD_OUT, "rxd");
	ctx->dsr = hk_pad_create(obj, HK_PAD_OUT, "dsr");
	ctx->cts = hk_pad_create(obj, HK_PAD_OUT, "cts");
	ctx->cd = hk_pad_create(obj, HK_PAD_OUT, "cd");
	ctx->ri = hk_pad_create(obj, HK_PAD_OUT, "ri");

	/* Update connection state pad */
	ctx->connected = hk_pad_create(obj, HK_PAD_OUT, "connected");
	hk_pad_update_int(ctx->connected, 0);

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
        if (ctx->io_only) {
                ctx->tty_chan.fd = fd;
        }
        else {
                io_channel_setup(&ctx->tty_chan, fd, (io_func_t) tty_recv, ctx);
        }

	/* Update connection state pad */
	hk_pad_update_int(ctx->connected, 1);

        /* Get MODEM input states */
        int flags = serial_modem_get(ctx->tty_chan.fd);
        if (flags >= 0) {
                tty_set_modem_outputs(ctx, flags);
        }

        /* Create MODEM input watch thread */
        ctx->thr_ok = 0;
        if (hk_pad_is_connected(ctx->dsr) || hk_pad_is_connected(ctx->cts) || hk_pad_is_connected(ctx->cd) || hk_pad_is_connected(ctx->ri)) {
                log_debug(1, "%s: Starting watch thread for Modem control signals", ctx->obj->name);
                int ret = tty_thread_init(ctx);
                if (ret == 0) {
                        ret = pthread_create(&ctx->thr, NULL, tty_thread, ctx);
                        if (ret < 0) {
                                log_str("ERROR: %s, pthread_create: %s", ctx->obj->name, strerror(errno));
                        }
                        else {
                                ctx->thr_ok = 1;
                        }
                }
        }
        else {
                log_debug(1, "%s: No pad connected for Modem control signals => no watch thread started", ctx->obj->name);
        }

        /* Set initial state for MODEM outputs */
        if (hk_pad_is_connected(ctx->rts)) {
                serial_modem_set(ctx->tty_chan.fd, SERIAL_RTS, ctx->rts->state);
        }
        if (hk_pad_is_connected(ctx->dtr)) {
                serial_modem_set(ctx->tty_chan.fd, SERIAL_DTR, ctx->dtr->state);
        }

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

        /* Assume io_only mode if neither TxD or RxD are used */
        if (!(hk_pad_is_connected(ctx->txd) || hk_pad_is_connected(ctx->rxd))) {
                ctx->tty_speed = 0;
                ctx->io_only = 1;
                log_str("tx/rx pads are not used => enable IO-Only mode");
        }

        /* Try to connect serial device ; Start periodic retry if it fails */
	if (tty_connect(ctx)) {
		ctx->timeout = sys_timeout(RETRY_DELAY, (sys_func_t) tty_retry, ctx);
	}
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

        if (pad == ctx->txd) {
                tty_send(ctx, value);
        }
        else if (pad == ctx->rts) {
                serial_modem_set(ctx->tty_chan.fd, SERIAL_RTS, atoi(value));
        }
        else if (pad == ctx->dtr) {
                serial_modem_set(ctx->tty_chan.fd, SERIAL_DTR, atoi(value));
        }
}


const hk_class_t _class_serial = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.start = _start,
	.input = _input,
};
