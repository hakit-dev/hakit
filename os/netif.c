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
#include <arpa/inet.h>

#include "log.h"
#include "sys.h"
#include "buf.h"
#include "tab.h"
#include "iputils.h"
#include "netif_watch.h"
#include "netif.h"


/*
 * Get HW addr from interface name
 */

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


/*
 * Scan all network interfaces
 */

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
	char addr[INET6_ADDRSTRLEN];
	char bcast[INET6_ADDRSTRLEN];
} netif_iface_t;


static int netif_get_interface(hk_tab_t *tab, struct ifaddrs* current)
{
	netif_iface_t *iface;
	char *hwaddr;

	hwaddr = netif_get_hwaddr(current->ifa_name);

	iface = hk_tab_push(tab);
	strncpy(iface->name, current->ifa_name, sizeof(iface->name)-1);
	strncpy(iface->hwaddr, hwaddr, sizeof(iface->hwaddr)-1);

	ip_addr(current->ifa_addr, iface->addr, sizeof(iface->addr));
	ip_addr(current->ifa_broadaddr, iface->bcast, sizeof(iface->bcast));

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


static void netif_check_interfaces(netif_env_t *ifs)
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
			if (ifs->callback != NULL) {
				ifs->callback(ifs->user_data);
			}
		}
	}
	else {
		hk_tab_cleanup(&tab);
	}
}


int netif_init(netif_env_t *ifs, netif_watch_callback_t change_callback, void *user_data)
{
	netif_collect_interfaces(&ifs->interfaces);
	netif_show_interfaces(&ifs->interfaces);

	ifs->callback = change_callback;
	ifs->user_data = user_data;

	netif_watch_init(&ifs->watch, (netif_watch_callback_t) netif_check_interfaces, ifs);

	return 0;
}


/*
 * Get HW addr from connected socket
 */

typedef struct {
	struct sockaddr sa;
	char *str;
} netif_socket_signature_ctx_t;


static int netif_socket_signature_scan(netif_socket_signature_ctx_t *ctx, struct ifaddrs* current)
{
	// Ignore if IP address does not match
	if (current->ifa_addr->sa_family != ctx->sa.sa_family) {
		return 0;
	}

	switch(ctx->sa.sa_family) {
	case AF_INET:
	        {
			struct sockaddr_in *addr1 = (struct sockaddr_in *) &ctx->sa;
			struct sockaddr_in *addr2 = (struct sockaddr_in *) current->ifa_addr;

			if (memcmp(&addr1->sin_addr, &addr2->sin_addr, sizeof(struct in_addr))) {
				return 0;
			}
		}
		break;

        case AF_INET6:
	        {
			struct sockaddr_in6 *addr1 = (struct sockaddr_in6 *) &ctx->sa;
			struct sockaddr_in6 *addr2 = (struct sockaddr_in6 *) current->ifa_addr;

			if (memcmp(&addr1->sin6_addr, &addr2->sin6_addr, sizeof(struct in6_addr))) {
				return 0;
			}
		}
		break;

		
        default:
		return 0;
	}

	if (ctx->str == NULL) {
		ctx->str = netif_get_hwaddr(current->ifa_name);
	}

	return -1;
}


char *netif_socket_signature(int sock)
{
	netif_socket_signature_ctx_t ctx;
	socklen_t addr_len = sizeof(ctx.sa);

	memset(&ctx, 0, sizeof(ctx));

	// Append hardware address
	if (getsockname(sock, &ctx.sa, &addr_len) < 0) {
		log_str("getsockname: %s", strerror(errno));
		return NULL;
	}

	netif_foreach_interface(&ctx, (netif_func_t) netif_socket_signature_scan);

	if (ctx.str == NULL) {
		ctx.str = strdup("00:00:00:00:00:00");
	}

	// Append IP address
	int len = strlen(ctx.str);
	ctx.str = realloc(ctx.str, len+INET6_ADDRSTRLEN+2);
	ctx.str[len++] = ' ';
	ip_addr(&ctx.sa, ctx.str+len, INET6_ADDRSTRLEN);

	return ctx.str;
}
