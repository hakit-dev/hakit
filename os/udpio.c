#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>

#include "log.h"
#include "iputils.h"
#include "udpio.h"


static int foreach_interface(void *user_data, int (*func)(void *user_data, struct ifaddrs *current))
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


int udp_send(int fd, char *addr, int port, char *buf, int size)
{
	struct sockaddr_in iremote;
	int ssize;

	memset(&iremote, 0, sizeof(iremote));
	iremote.sin_family = AF_INET;
	iremote.sin_addr.s_addr = inet_addr(addr);
	iremote.sin_port = htons(port);

	ssize = sendto(fd, buf, size, 0, (struct sockaddr *) &iremote, sizeof(iremote));
	if (ssize < 0) {
		log_str("ERROR: sendto(%s:%d): %s", addr, port, strerror(errno));
	}

	return ssize;
}


typedef struct {
	int fd;
	int port;
	char *buf;
	int size;
} udp_send_bcast_ctx_t;


static int udp_send_bcast_addr(void *pctx, struct ifaddrs* current)
{
	udp_send_bcast_ctx_t *ctx = pctx;
	struct sockaddr_in iremote;
	int ret;

	memcpy(&iremote, current->ifa_broadaddr, sizeof(iremote));
	iremote.sin_port = htons(ctx->port);

	log_debug(2, "Sending UDP broadcast to %s", ip_addr(NULL, &iremote));

	ret = sendto(ctx->fd, ctx->buf, ctx->size, 0, (struct sockaddr *) &iremote, sizeof(iremote));
	if (ret < 0) {
		log_str("ERROR: sendto(%s): %s", ip_addr(NULL, &iremote), strerror(errno));
		return -1;
	}

	return 0;
}


int udp_send_bcast(int fd, int port, char *buf, int size)
{
	int enable;
	udp_send_bcast_ctx_t ctx;
	int ret = -1;

	/* Enable broadcast on this socket */
	enable = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (void *) &enable, sizeof(enable)) < 0) {
		log_str("ERROR: setsockopt(BROADCAST): %s", strerror(errno));
		return -1;
	}

	ctx.fd = fd;
	ctx.port = port;
	ctx.buf = buf;
	ctx.size = size;
	ret = foreach_interface(&ctx, udp_send_bcast_addr);

	/* Disable broadcast on this socket */
	enable = 0;
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (void *) &enable, sizeof(enable)) < 0) {
		log_str("ERROR: setsockopt(BROADCAST): %s", strerror(errno));
		return -1;
	}

	return ret;
}


static int udp_check_interfaces_addr(void *pcount, struct ifaddrs* current)
{
	int *count = pcount;
	struct sockaddr_in *addr = (struct sockaddr_in *) current->ifa_addr;
	unsigned long addr_ = ntohl(addr->sin_addr.s_addr);
	struct sockaddr_in *bcast = (struct sockaddr_in *) current->ifa_broadaddr;
	unsigned long bcast_ = ntohl(bcast->sin_addr.s_addr);

	if (*count == 0) {
		log_str("Available interfaces:");
	}

	log_str("  %s: %lu.%lu.%lu.%lu (%lu.%lu.%lu.%lu)", current->ifa_name,
		(addr_ >> 24) & 0xFF, (addr_ >> 16) & 0xFF, (addr_ >> 8) & 0xFF, addr_ & 0xFF,
		(bcast_ >> 24) & 0xFF, (bcast_ >> 16) & 0xFF, (bcast_ >> 8) & 0xFF, bcast_ & 0xFF);

	(*count)++;

	return 0;
}


int udp_check_interfaces(void)
{
	int count = 0;

	foreach_interface(&count, udp_check_interfaces_addr);

	if (count == 0) {
		log_str("No broadcast interface found");
	}

	return count;
}


static int udp_srv_local_addr(void *piremote, struct ifaddrs* current)
{
	struct sockaddr_in *iremote = piremote;
	struct sockaddr_in *addr = (struct sockaddr_in *) current->ifa_addr;

	if (iremote->sin_addr.s_addr == addr->sin_addr.s_addr) {
		return -1;
	}

	return 0;
}


static int udp_srv_event(udp_srv_t *srv, int fd)
{
	unsigned int iremote_size = sizeof(srv->iremote);
	char buf[1500];
	int len;

	memset(&srv->iremote, 0, iremote_size);

	len = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *) &srv->iremote, &iremote_size);
	if (len < 0) {
		log_str("ERROR: recvfrom: %s", strerror(errno));
		return 0;
	}

	/* Reject datagrams from myself */
	if (foreach_interface(&srv->iremote, udp_srv_local_addr)) {
		log_debug(2, "udp_srv_event: filtered packet from %s", ip_addr(NULL, &srv->iremote));
		return 1;
	}

	log_debug(2, "udp_srv_event: %d bytes from %s", len, ip_addr(NULL, &srv->iremote));
	if (srv->chan.func != NULL) {
		srv->chan.func(srv->chan.user_data, buf, len);
	}

	return 1;
}


void udp_srv_clear(udp_srv_t *srv)
{
	io_channel_clear(&srv->chan);
	memset(&srv->iremote, 0, sizeof(srv->iremote));
}


int udp_srv_init(udp_srv_t *srv, int port, io_func_t func, void *user_data)
{
	struct sockaddr_in ilocal;
	int fd;

	udp_srv_clear(srv);

	/* Create network socket */
	fd = socket(PF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		log_str("ERROR: socket(PF_INET, SOCK_DGRAM): %s", strerror(errno));
		return -1;
	}

	int optval = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	/* Bind network socket */
	memset(&ilocal, 0, sizeof(ilocal));
	ilocal.sin_family = AF_INET;
	ilocal.sin_addr.s_addr = htonl(INADDR_ANY);
	ilocal.sin_port = htons(port);

	if (bind(fd, (struct sockaddr *) &ilocal, sizeof(ilocal)) == -1) { 
		log_str("ERROR: bind(%d): %s", port, strerror(errno));
		close(fd);
		return -1;
	}

	log_str("Listening to UDP datagrams from port %d", port);

	/* Prevent child processes from inheriting this socket */
	fcntl(fd, F_SETFD, FD_CLOEXEC);

	io_blocking(fd, 0);

	srv->chan.fd = fd;
	srv->chan.tag = sys_io_watch(fd, (sys_io_func_t) udp_srv_event, srv);
	srv->chan.func = func;
	srv->chan.user_data = user_data;
	srv->port = port;

	return 0;
}


int udp_srv_send_reply(udp_srv_t *srv, char *buf, int size)
{
	int ssize;

	ssize = sendto(srv->chan.fd, buf, size, 0, (struct sockaddr *) &srv->iremote, sizeof(srv->iremote));
	if (ssize < 0) {
		log_str("ERROR: udp_srv_write(%s): %s", ip_addr(NULL, &srv->iremote), strerror(errno));
		return -1;
	}

	return 0;
}


int udp_srv_send_to(udp_srv_t *srv, char *buf, int size, char *addr)
{
	return udp_send(srv->chan.fd, addr, srv->port, buf, size);
}


int udp_srv_send_bcast(udp_srv_t *srv, char *buf, int size)
{
	int ssize;

	ssize = udp_send_bcast(srv->chan.fd, srv->port, buf, size);
	if (ssize < 0) {
		log_str("ERROR: udp_srv_bcast(%d): %s", srv->port, strerror(errno));
		return -1;
	}

	return 0;
}


struct sockaddr_in *udp_srv_remote(udp_srv_t *srv)
{
	return &srv->iremote;
}


void udp_srv_shutdown(udp_srv_t *srv)
{
	if (srv->chan.fd > 0) {
		close(srv->chan.fd);
	}

	udp_srv_clear(srv);
}
