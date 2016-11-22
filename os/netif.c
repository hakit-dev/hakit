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
#include "sys.h"
#include "tab.h"
#include "iputils.h"
#include "netif.h"


#define INTERFACE_CHECK_DELAY 60000   // Check for available network interfaces once per minute
#define HWADDR_SIZE 6


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
	char name[16];
	char hwaddr[24];
	char addr[32];
	char bcast[32];
} netif_iface_t;


static int netif_get_interface(hk_tab_t *tab, struct ifaddrs* current)
{
	struct sockaddr_in *addr = (struct sockaddr_in *) current->ifa_addr;
	unsigned long addr_ = ntohl(addr->sin_addr.s_addr);
	struct sockaddr_in *bcast = (struct sockaddr_in *) current->ifa_broadaddr;
	unsigned long bcast_ = ntohl(bcast->sin_addr.s_addr);
	netif_iface_t *iface;
	char *hwaddr;

	hwaddr = netif_get_hwaddr(current->ifa_name);

	iface = hk_tab_push(tab);
	strncpy(iface->name, current->ifa_name, sizeof(iface->name)-1);
	strncpy(iface->hwaddr, hwaddr, sizeof(iface->hwaddr)-1);
	snprintf(iface->addr, sizeof(iface->addr), "%lu.%lu.%lu.%lu",
		 (addr_ >> 24) & 0xFF, (addr_ >> 16) & 0xFF, (addr_ >> 8) & 0xFF, addr_ & 0xFF);
	snprintf(iface->bcast, sizeof(iface->bcast), "%lu.%lu.%lu.%lu",
		 (bcast_ >> 24) & 0xFF, (bcast_ >> 16) & 0xFF, (bcast_ >> 8) & 0xFF, bcast_ & 0xFF);

	if (hwaddr != NULL) {
		free(hwaddr);
	}

	return 0;
}


static void netif_collect_interfaces(hk_tab_t *tab)
{
	hk_tab_init(tab, sizeof(netif_iface_t));
	netif_foreach_interface(tab, (netif_func_t) netif_get_interface);
}


static void netif_show_interfaces(hk_tab_t *tab)
{
	if (tab->nmemb == 0) {
		log_str("No active network interface found");
	}
	else {
		int i;

		log_str("Available interfaces:");
		for (i = 0; i < tab->nmemb; i++) {
			netif_iface_t *iface = HK_TAB_PTR(*tab, netif_iface_t, i);
			log_str("  %s: HWaddr=%s addr=%s Bcast=%s", iface->name, iface->hwaddr, iface->addr, iface->bcast);
		}
	}
}


static int netif_check_interfaces(netif_env_t *ifs)
{
	hk_tab_t tab;
	int changed = 0;

	netif_collect_interfaces(&tab);

	if (tab.nmemb == ifs->interfaces.nmemb) {
		int i;

		for (i = 0; (i < tab.nmemb) && (changed == 0); i++) {
			netif_iface_t *iface1 = HK_TAB_PTR(tab, netif_iface_t, i);
			netif_iface_t *iface2 = HK_TAB_PTR(ifs->interfaces, netif_iface_t, i);
			changed = memcmp(iface1, iface2, sizeof(netif_iface_t));
		}
	}
	else {
		changed = 1;
	}
	
	if (changed) {
		hk_tab_cleanup(&ifs->interfaces);
		ifs->interfaces = tab;

		log_str("Network interface change detected");
		netif_show_interfaces(&ifs->interfaces);

		if (ifs->interfaces.nmemb > 0) {
			if (ifs->change_callback != NULL) {
				ifs->change_callback(ifs->user_data);
			}
		}
	}
	else {
		hk_tab_cleanup(&tab);
	}

	return 1;
}


int netif_init(netif_env_t *ifs, netif_change_callback_t cb, void *user_data)
{
	netif_collect_interfaces(&ifs->interfaces);
	netif_show_interfaces(&ifs->interfaces);

	ifs->check_tag = sys_timeout(INTERFACE_CHECK_DELAY, (sys_func_t) netif_check_interfaces, ifs);
	ifs->change_callback = cb;
	ifs->user_data = user_data;

	return 0;
}
