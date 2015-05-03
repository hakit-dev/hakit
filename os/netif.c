/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <ifaddrs.h>

#include "log.h"
#include "iputils.h"
#include "netif.h"


int netif_foreach_interface(void *user_data, int (*func)(void *user_data, struct ifaddrs *current))
{
	struct ifaddrs* ifap = NULL;
	struct ifaddrs* current;
	int ret = 0;

	if (getifaddrs(&ifap)) {
		log_str("ERROR: getifaddrs: %s", strerror(errno));
		return -1;
	}

	if (ifap == NULL) {
		log_str("ERROR: no network interface found");
		return -1;
	}

	current = ifap;
	while (current != NULL) {
		if ((current->ifa_flags & (IFF_UP|IFF_BROADCAST)) &&
		    !(current->ifa_flags & IFF_LOOPBACK) &&
		    (current->ifa_broadaddr != NULL)) {
			if (current->ifa_broadaddr->sa_family == AF_INET) {
				if (func != NULL) {
					if (func(user_data, current) < 0) {
						ret = -1;
						goto DONE;
					}
				}
			}
		}
		current = current->ifa_next;
	}

DONE:
	freeifaddrs(ifap);
	ifap = NULL;

	return ret;
}


typedef struct {
	int count;
} udp_check_interfaces_ctx_t;


static int netif_check_interfaces_addr(udp_check_interfaces_ctx_t *ctx, struct ifaddrs* current)
{
	ctx->count++;
	return 0;
}


int netif_check_interfaces(void)
{
	udp_check_interfaces_ctx_t ctx = {
		.count = 0,
	};

	netif_foreach_interface(&ctx, (netif_func_t) netif_check_interfaces_addr);

	return ctx.count;
}


static int netif_show_interfaces_addr(udp_check_interfaces_ctx_t *ctx, struct ifaddrs* current)
{
	struct sockaddr_in *addr = (struct sockaddr_in *) current->ifa_addr;
	unsigned long addr_ = ntohl(addr->sin_addr.s_addr);
	struct sockaddr_in *bcast = (struct sockaddr_in *) current->ifa_broadaddr;
	unsigned long bcast_ = ntohl(bcast->sin_addr.s_addr);

	if (ctx->count == 0) {
		log_str("Available interfaces:");
	}

	log_str("  %s: %lu.%lu.%lu.%lu (%lu.%lu.%lu.%lu)", current->ifa_name,
		(addr_ >> 24) & 0xFF, (addr_ >> 16) & 0xFF, (addr_ >> 8) & 0xFF, addr_ & 0xFF,
		(bcast_ >> 24) & 0xFF, (bcast_ >> 16) & 0xFF, (bcast_ >> 8) & 0xFF, bcast_ & 0xFF);

	ctx->count++;

	return 0;
}


int netif_show_interfaces(void)
{
	udp_check_interfaces_ctx_t ctx = {
		.count = 0,
	};

	netif_foreach_interface(&ctx, (netif_func_t) netif_show_interfaces_addr);

	if (ctx.count == 0) {
		log_str("No network interface found");
	}

	return ctx.count;
}
