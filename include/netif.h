/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_NETIF_H__
#define __HAKIT_NETIF_H__

/******************/
/* Static methods */
/******************/

#include <ifaddrs.h>

typedef int (*netif_func_t)(void *user_data, struct ifaddrs *current);

extern int netif_foreach_interface(void *user_data, netif_func_t func);

extern char *netif_get_hwaddr(char *if_name);


/**************************/
/* Contextualized methods */
/**************************/

#include "tab.h"
#include "netif_watch.h"

typedef void (*netif_change_callback_t)(void *user_data);

typedef struct {
	hk_tab_t interfaces;
	netif_watch_t watch;
	netif_change_callback_t callback;
	void *user_data;
} netif_env_t;

extern int netif_init(netif_env_t *ifs, netif_change_callback_t change_callback, void *user_data);

#endif /* __HAKIT_NETIF_H__ */
