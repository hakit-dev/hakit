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

#include <ifaddrs.h>

typedef int (*netif_func_t)(void *user_data, struct ifaddrs *current);

extern int netif_foreach_interface(void *user_data, netif_func_t func);
extern int netif_check_interfaces(void);
extern int netif_show_interfaces(void);

#endif /* __HAKIT_NETIF_H__ */
