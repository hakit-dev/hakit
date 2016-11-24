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

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "log.h"
#include "sys.h"
#include "netif_watch.h"


#define NETIF_WATCH_LOWPASS_DELAY 5000


static int netif_watch_process(netif_watch_t *w)
{
	w->timeout_tag = 0;

	log_debug(2, "Network interface change triggered");

	if (w->callback != NULL) {
		w->callback(w->user_data);
	}

	return 0;
}


static int netif_watch_event(netif_watch_t *w, int fd)
{
	struct iovec iov;
	struct msghdr smsg;
	struct cmsghdr *cmsg;
	struct ucred *cred;
	char cred_msg[CMSG_SPACE(sizeof(struct ucred))];
	ssize_t bytes;

	log_debug(3, "netif_watch_event");

	iov.iov_base = w->buf;
	iov.iov_len = sizeof(w->buf);

	smsg.msg_name = NULL;
	smsg.msg_namelen = 0;
	smsg.msg_iov = &iov;
	smsg.msg_iovlen = 1;
	smsg.msg_control = cred_msg;
	smsg.msg_controllen = sizeof(cred_msg);
	smsg.msg_flags = MSG_DONTWAIT;

	if ((bytes = recvmsg(fd, &smsg, 0)) < 0) {
		if ((errno == EAGAIN) || (errno == EINTR)) {
			return 1;
		}

		log_str("ERROR: netlink recvmsg(): %s", strerror(errno));
		return 0;
	}

	cmsg = CMSG_FIRSTHDR(&smsg);
	if ((cmsg == NULL) || (cmsg->cmsg_type != SCM_CREDENTIALS)) {
		log_str("WARNING: netlink recvmsg(): No sender credentials received, ignoring data.");
		return 1;
	}

	cred = (struct ucred*) CMSG_DATA(cmsg);
	if (cred->pid != 0) {
		return 1;
	}

	/* Throw event with lowpass filtering */
	if (w->timeout_tag) {
		sys_remove(w->timeout_tag);
	}
	w->timeout_tag = sys_timeout(NETIF_WATCH_LOWPASS_DELAY, (sys_func_t) netif_watch_process, w);

	return 1;
}


int netif_watch_init(netif_watch_t *w, netif_watch_callback_t watch_callback, void *user_data)
{
	const int on = 1;
	struct sockaddr_nl addr;

	w->sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE); 
	if (w->sock < 0) {
		log_str("ERROR: netif netlink socket: %s", strerror(errno));
		goto failed;
	}

	memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = RTMGRP_LINK|RTMGRP_IPV4_IFADDR|RTMGRP_IPV6_IFADDR;
	addr.nl_pid = 0;

	if (bind(w->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		log_str("ERROR: netif netlink bind: %s", strerror(errno));
		goto failed;
	}

	if (setsockopt(w->sock, SOL_SOCKET, SO_PASSCRED, &on, sizeof(on)) < 0) {
		log_str("ERROR: netif netlink setsockopt: %s", strerror(errno));
		goto failed;
	}

	w->io_tag = sys_io_watch(w->sock, (sys_io_func_t) netif_watch_event, w);
	w->callback = watch_callback;
	w->user_data = user_data;

	return 0;

failed:
	if (w->sock >= 0) {
		close(w->sock);
		w->sock = -1;
	}

	return -1;
}
