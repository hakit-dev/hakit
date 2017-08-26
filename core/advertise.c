/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2017 Sylvain Giroudon
 *
 * HAKit Advertising Protocol
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>

#include "buf.h"
#include "tab.h"
#include "log.h"
#include "sys.h"
#include "netif.h"
#include "iputils.h"
#include "advertise.h"


#define ADVERTISE_DELAY 1000   // Delay before advertising a newly registered sink/source

#define DISCOVER_SIGNATURE  0xAC   // Advertising packet signature
#define DISCOVER_REQUEST    0x04   // Advertising field type: discover request


static void hk_advertise_send_request(hk_advertise_t *adv)
{
	buf_t buf;

	log_debug(3, "hk_advertise_send_request");

	buf_init(&buf);
	buf_append_byte(&buf, DISCOVER_SIGNATURE);
	buf_append_byte(&buf, DISCOVER_REQUEST);

	/* Send UDP packet as broadcast */
	udp_srv_send_bcast(&adv->udp_srv, (char *) buf.base, buf.len);

	buf_cleanup(&buf);
}


static void hk_advertise_send_reply(hk_advertise_t *adv, unsigned char type)
{
	buf_t buf;

	log_debug(3, "hk_advertise_send_reply %02X", type);

	buf_init(&buf);
	buf_append_byte(&buf, DISCOVER_SIGNATURE);
	buf_append_byte(&buf, type);

	/* Send UDP packet as a reply to previously received request */
	udp_srv_send_reply(&adv->udp_srv, (char *) buf.base, buf.len);

	buf_cleanup(&buf);
}


static void hk_advertise_event(hk_advertise_t *adv, unsigned char *buf, int size)
{
	char remote_ip[64];
	int i;

	/* Get remote IP address */
	ip_addr((struct sockaddr *) udp_srv_remote(&adv->udp_srv), remote_ip, sizeof(remote_ip));

	log_debug(3, "hk_advertise_event: %d bytes from %s", size, remote_ip);
	log_debug_data(buf, size);

	if (size < 2) {
		log_str("WARNING: Received too short UDP packet");
		return;
	}

	if (buf[0] != DISCOVER_SIGNATURE) {
		log_str("WARNING: Received UDP packet with wrong magic number");
		return;
	}

	switch (buf[1]) {
	case DISCOVER_REQUEST:
		/* Send DISCOVER reply if we have some sinks to export */
		if (adv->have_sink) {
			hk_advertise_send_reply(adv, DISCOVER_HKCP);
		}
		/* Intentionally no break here : the following instructions are common
		   for both DISCOVER_REQUEST and DISCOVER_HKCP cases. */
	case DISCOVER_HKCP:
		for (i = 0; i < adv->handlers.nmemb; i++) {
			hk_advertise_handler_t *hdl = HK_TAB_PTR(adv->handlers, hk_advertise_handler_t, i);
			if ((hdl->type == 0x00) || (hdl->type == DISCOVER_HKCP)) {
				hdl->func(hdl->user_data, remote_ip);
			}
		}
		break;
	default:
		log_str("WARNING: Received UDP packet with unknown type (%02X)", buf[1]);
		break;
	}
}


static int hk_advertise_now(hk_advertise_t *adv)
{
	adv->tag = 0;

	// Nothing to do in local mode
	if (adv->udp_srv.chan.fd > 0) {
		hk_advertise_send_request(adv);
	}

	return 0;
}


static void hk_advertise(hk_advertise_t *adv)
{
	if (adv->tag != 0) {
		sys_remove(adv->tag);
		adv->tag = 0;
	}

	if (adv->udp_srv.chan.fd > 0) {
		log_debug(2, "Will send advertisement request in %lu ms", ADVERTISE_DELAY);
		adv->tag = sys_timeout(ADVERTISE_DELAY, (sys_func_t) hk_advertise_now, adv);
	}
}


void hk_advertise_sink(hk_advertise_t *adv)
{
	adv->have_sink = 1;
	hk_advertise(adv);
}


void hk_advertise_source(hk_advertise_t *adv)
{
	adv->have_source = 1;
	hk_advertise(adv);
}


int hk_advertise_init(hk_advertise_t *adv, int port)
{
	memset(adv, 0, sizeof(hk_advertise_t));

	hk_tab_init(&adv->handlers, sizeof(hk_advertise_handler_t));

	/* Init UDP server */
	udp_srv_clear(&adv->udp_srv);
	if (udp_srv_init(&adv->udp_srv, port, (io_func_t) hk_advertise_event, adv)) {
		return -1;
	}
		
	/* Init network interface check */
	netif_init(&adv->ifs, (netif_change_callback_t) hk_advertise, adv);

	return 0;
}


void hk_advertise_shutdown(hk_advertise_t *adv)
{
	netif_shutdown(&adv->ifs);

	if (adv->udp_srv.chan.fd > 0) {
		udp_srv_shutdown(&adv->udp_srv);
	}

	hk_tab_cleanup(&adv->handlers);
}


void hk_advertise_handler(hk_advertise_t *adv, hk_advertise_type_t type,
			  hk_advertise_func_t func, void *user_data)
{
	hk_advertise_handler_t *handler = hk_tab_push(&adv->handlers);
	handler->type = type;
	handler->func = func;
	handler->user_data = user_data;
}
