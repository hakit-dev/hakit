/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Network interface change detection
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_NETIF_WATCH_H__
#define __HAKIT_NETIF_WATCH_H__

#include "sys.h"

typedef void (*netif_watch_callback_t)(void *user_data);

typedef struct {
	int sock;
	char buf[64*1024];
	sys_tag_t io_tag;
	sys_tag_t timeout_tag;
	netif_watch_callback_t callback;
	void *user_data;
} netif_watch_t;

extern int netif_watch_init(netif_watch_t *w, netif_watch_callback_t watch_callback, void *user_data);
extern void netif_watch_shutdown(netif_watch_t *w);

#endif // __HAKIT_NETIF_WATCH_H__
