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
#include <malloc.h>

#include "buf.h"
#include "tab.h"
#include "log.h"
#include "sys.h"
#include "netif.h"
#include "iputils.h"
#include "advertise.h"


#define ADVERTISE_DELAY 1000   // Delay before advertising a newly registered sink/source

#define DISCOVER_SIGNATURE 0xAC   // Advertising packet signature
#define DISCOVER_REQUEST   0x04   // Advertising packet type: request
#define DISCOVER_REPLY     0x05   // Advertising packet type: reply


static void hk_advertise_send_request(hk_advertise_t *adv, hk_advertise_proto_t proto, char *str)
{
	buf_t buf;

	log_debug(3, "hk_advertise_send_hkcp_request");

	buf_init(&buf);
	buf_append_byte(&buf, DISCOVER_SIGNATURE);
	buf_append_byte(&buf, DISCOVER_REQUEST);
	buf_append_byte(&buf, proto);
	if (str != NULL) {
		buf_append(&buf, (unsigned char *) str, strlen(str));
	}

	/* Send UDP packet as broadcast */
	udp_srv_send_bcast(&adv->udp_srv, (char *) buf.base, buf.len);

	buf_cleanup(&buf);
}


static void hk_advertise_send_reply(hk_advertise_t *adv, hk_advertise_proto_t proto, char *str)
{
	buf_t buf;

	log_debug(3, "hk_advertise_send_hkcp_reply");

	buf_init(&buf);
	buf_append_byte(&buf, DISCOVER_SIGNATURE);
	buf_append_byte(&buf, DISCOVER_REPLY);
	buf_append_byte(&buf, proto);
	if (str != NULL) {
		buf_append(&buf, (unsigned char *) str, strlen(str));
	}

	/* Send UDP packet as a reply to previously received request */
	udp_srv_send_reply(&adv->udp_srv, (char *) buf.base, buf.len);

	buf_cleanup(&buf);
}


static void hk_advertise_event(hk_advertise_t *adv, unsigned char *buf, int size)
{
	char remote_ip[64];
	char *str = NULL;
	int len = 0;
	int i;

	/* Get remote IP address */
	ip_addr((struct sockaddr *) udp_srv_remote(&adv->udp_srv), remote_ip, sizeof(remote_ip));

	log_debug(3, "hk_advertise_event: %d bytes from %s", size, remote_ip);
	log_debug_data(buf, size);

	if (size < 3) {
		log_str("WARNING: Received too short UDP packet");
		return;
	}

	if (buf[0] != DISCOVER_SIGNATURE) {
		log_str("WARNING: Received UDP packet with wrong magic number");
		return;
	}

	if (size > 3) {
		len = size - 3;
		str = (char *) malloc(len+1);
		memcpy(str, &buf[3], len);
		str[len] = '\0';
	}

	switch (buf[1]) {
	case DISCOVER_REQUEST:
		if (adv->proto != 0x00) {
			hk_advertise_send_reply(adv, adv->proto, adv->mqtt_broker);
		}

		/* Intentionally no break here : the following instructions are common
		   for both DISCOVER_REQUEST and DISCOVER_REPLY cases. */
	case DISCOVER_REPLY:
		for (i = 0; i < adv->handlers.nmemb; i++) {
			hk_advertise_handler_t *hdl = HK_TAB_PTR(adv->handlers, hk_advertise_handler_t, i);
			if (buf[2] & hdl->proto) {
				hdl->func(hdl->user_data, remote_ip, str);
			}
		}
		break;
	default:
		log_str("WARNING: Received UDP packet with unknown type (%02X)", buf[1]);
		break;
	}

	if (str != NULL) {
		free(str);
		str = NULL;
	}
}


static int hk_advertise_now(hk_advertise_t *adv)
{
	adv->tag = 0;

	hk_advertise_send_request(adv, adv->proto, adv->mqtt_broker);

	return 0;
}


static void hk_advertise(hk_advertise_t *adv)
{
	if (adv->tag != 0) {
		sys_remove(adv->tag);
		adv->tag = 0;
	}

	if (adv->udp_srv.chan.fd > 0) {
		if (adv->proto != 0x00) {
			log_debug(2, "Will send advertisement request in %lu ms", ADVERTISE_DELAY);
			adv->tag = sys_timeout(ADVERTISE_DELAY, (sys_func_t) hk_advertise_now, adv);
		}
	}
}


void hk_advertise_hkcp(hk_advertise_t *adv)
{
	log_debug(3, "hk_advertise_hkcp");

	adv->proto |= ADVERTISE_PROTO_HKCP;

	hk_advertise(adv);
}


void hk_advertise_mqtt(hk_advertise_t *adv, char *broker)
{
	log_debug(3, "hk_advertise_mqtt");

	if (adv->mqtt_broker != NULL) {
		free(adv->mqtt_broker);
		adv->mqtt_broker = NULL;
	}
	if (broker != NULL) {
		adv->mqtt_broker = strdup(broker);
	}

	adv->proto |= ADVERTISE_PROTO_MQTT;

	hk_advertise(adv);
}


int hk_advertise_init(hk_advertise_t *adv, int port)
{
	log_debug(3, "hk_advertise_init");

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

	if (adv->mqtt_broker != NULL) {
		free(adv->mqtt_broker);
		adv->mqtt_broker = NULL;
	}
}


void hk_advertise_handler(hk_advertise_t *adv, hk_advertise_proto_t proto,
			  hk_advertise_func_t func, void *user_data)
{
	hk_advertise_handler_t *handler = hk_tab_push(&adv->handlers);
	handler->proto = proto;
	handler->func = func;
	handler->user_data = user_data;
}
