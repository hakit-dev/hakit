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
#include <malloc.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <ifaddrs.h>

#include "log.h"
#include "iputils.h"
#include "netif.h"


#define HWADDR_SIZE 6


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
	char *hwaddr;

	if (ctx->count == 0) {
		log_str("Available interfaces:");
	}

	hwaddr = netif_get_hwaddr(current->ifa_name);

	log_str("  %s: HWaddr=%s addr=%lu.%lu.%lu.%lu Bcast=%lu.%lu.%lu.%lu", current->ifa_name, hwaddr,
		(addr_ >> 24) & 0xFF, (addr_ >> 16) & 0xFF, (addr_ >> 8) & 0xFF, addr_ & 0xFF,
		(bcast_ >> 24) & 0xFF, (bcast_ >> 16) & 0xFF, (bcast_ >> 8) & 0xFF, bcast_ & 0xFF);

	if (hwaddr != NULL) {
		free(hwaddr);
	}

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


char *netif_get_hwaddr(char *if_name)
{
	int sock;
	struct ifreq ifr;
	char *str = NULL;

	/* Open a work socket */
	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		log_str("Cannot create socket: %s", strerror(errno));
		return NULL;
	}

	strcpy(ifr.ifr_name, if_name);

	if (ioctl(sock, SIOCGIFHWADDR, &ifr) >= 0) {
		int size = HWADDR_SIZE*3;
		int len = 0;
		int i;

		str = malloc(size);

		for (i = 0; i < HWADDR_SIZE; i++) {
			len += snprintf(str+len, size-len, "%02x:", ifr.ifr_hwaddr.sa_data[i] & 0xFF);
		}

		if (len > 0) {
			len--;
		}
		str[len] = '\0';
	}
	else {
		log_str("Cannot get hardware address for %s: %s", if_name, strerror(errno));
	}

	/* Close the work socket */
	close(sock);

	return str;
}
