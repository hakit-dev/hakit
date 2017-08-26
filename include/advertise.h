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

#ifndef __HAKIT_ADVERTISE_H__
#define __HAKIT_ADVERTISE_H__

#include "sys.h"
#include "udpio.h"
#include "netif.h"

typedef enum {
	DISCOVER_HKCP=0x05
} hk_advertise_type_t;

typedef void (*hk_advertise_func_t)(void *user_data, char *remote_ip);

typedef struct {
	hk_advertise_type_t type;
	hk_advertise_func_t func;
	void *user_data;
} hk_advertise_handler_t;

typedef struct {
	netif_env_t ifs;
	udp_srv_t udp_srv;
	sys_tag_t tag;
	hk_tab_t handlers;   // Table of (hk_advertise_handler_t)
	int have_sink;
	int have_source;
} hk_advertise_t;

extern int hk_advertise_init(hk_advertise_t *adv, int port);
extern void hk_advertise_shutdown(hk_advertise_t *adv);
extern void hk_advertise_handler(hk_advertise_t *adv, hk_advertise_type_t type,
				 hk_advertise_func_t func, void *user_data);
extern void hk_advertise_sink(hk_advertise_t *adv);
extern void hk_advertise_source(hk_advertise_t *adv);

#endif /* __HAKIT_ADVERTISE_H__ */
