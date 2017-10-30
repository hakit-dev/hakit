/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "types.h"
#include "options.h"
#include "log.h"
#include "sys.h"


#define NSOURCES 32

typedef enum {
	SYS_TYPE_NONE=0,
	SYS_TYPE_TIMEOUT,
	SYS_TYPE_IO,
	SYS_TYPE_CHILD,
	SYS_TYPE_QUIT,
	SYS_TYPE_REMOVED,
} sys_type_t;


typedef struct {
	sys_tag_t tag;
	sys_func_t func;
	void *arg;
	sys_type_t type;
	union {
		struct {
			struct pollfd pollfd;
			int poll;
		} io;
		struct {
			unsigned long delay;
			unsigned long long t;
		} timeout;
		struct {
			pid_t pid;
			int status;
		} child;
	} d;
} sys_source_t;


static sys_tag_t last_tag = 0;
static volatile int quit_requested = 0;
static sys_source_t sources[NSOURCES] = {};


static void sys_source_clear(sys_source_t *src)
{
	memset(src, 0, sizeof(sys_source_t));
}


static sys_source_t *sys_source_add(sys_func_t func, void *arg)
{
	sys_source_t *src = NULL;
	int i;

	/* Try to spot a free slot */
	for (i = 0; i < NSOURCES; i++) {
		if (sources[i].tag == 0) {
			src = &sources[i];
			break;
		}
	}

	/* Allocate a new slot if none is available */
	if (src == NULL) {
		log_str("PANIC: No more system sources available");
		return NULL;
	}

	sys_source_clear(src);

	last_tag++;
	src->tag = last_tag;
	src->func = func;
	src->arg = arg;

	return src;
}


void sys_remove(sys_tag_t tag)
{
	int i;

	log_debug(3, "sys_remove(%u)", tag);

	for (i = 0; i < NSOURCES; i++) {
		sys_source_t *src = &sources[i];
		if (src->tag == tag) {
			src->type = SYS_TYPE_REMOVED;
			src->func = NULL;
			src->arg = NULL;
			break;
		}
	}
}


static sys_source_t *sys_retrieve_fd(int fd)
{
	int i;

	for (i = 0; i < NSOURCES; i++) {
		sys_source_t *src = &sources[i];
		if ((src->type == SYS_TYPE_IO) && (src->d.io.pollfd.fd == fd)) {
			return src;
		}
	}

	return NULL;
}


void sys_remove_fd(int fd)
{
	sys_source_t *src = sys_retrieve_fd(fd);

	log_debug(3, "sys_remove_fd(%d)", fd);

	if (src != NULL) {
		src->type = SYS_TYPE_REMOVED;
		src->func = NULL;
		src->arg = NULL;
	}
}


static unsigned long long sys_now(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0) {
		log_str("PANIC: gettimeofday: %s", strerror(errno));
		return 0;
	}

	return (((unsigned long long) tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}


static sys_source_t *sys_io_source_add(int fd, sys_func_t func, void *arg)
{
	sys_source_t *src = sys_retrieve_fd(fd);

	if (src == NULL) {
		src = sys_source_add((sys_func_t) func, arg);
	}

	return src;
}


sys_tag_t sys_io_watch(int fd, sys_io_func_t func, void *arg)
{
	sys_source_t *src = sys_io_source_add(fd, (sys_func_t) func, arg);

	if (src == NULL) {
		return 0;
	}

	src->type = SYS_TYPE_IO;
	src->d.io.pollfd.fd = fd;
	src->d.io.pollfd.events = POLLIN | POLLPRI | POLLRDHUP | POLLERR | POLLHUP | POLLNVAL;
	src->d.io.pollfd.revents = 0;
	src->d.io.poll = 0;

	log_debug(3, "sys_io_watch(%d) => tag=%u", fd, src->tag);
	return src->tag;
}


sys_tag_t sys_io_poll(int fd, unsigned int events, sys_poll_func_t func, void *arg)
{
	sys_source_t *src = sys_io_source_add(fd, (sys_func_t) func, arg);

	if (src == NULL) {
		return 0;
	}

	src->type = SYS_TYPE_IO;
	src->d.io.pollfd.fd = fd;
	src->d.io.pollfd.events = events;
	src->d.io.pollfd.revents = 0;
	src->d.io.poll = 1;

	log_debug(3, "sys_io_poll(%d,%02X) => tag=%u", fd, events, src->tag);

	return src->tag;
}


sys_tag_t sys_timeout(unsigned long delay, sys_func_t func, void *arg)
{
	sys_source_t *src = sys_source_add(func, arg);

	if (src == NULL) {
		return 0;
	}

	src->type = SYS_TYPE_TIMEOUT;
	src->d.timeout.delay = delay;
	src->d.timeout.t = delay + sys_now();
	log_debug(3, "sys_timeout(%lu) => tag=%u t=%llu", delay, src->tag, src->d.timeout.t);
	return src->tag;
}


sys_tag_t sys_child_watch(pid_t pid, sys_child_func_t func, void *arg)
{
	sys_source_t *src = sys_source_add((sys_func_t) func, arg);

	if (src == NULL) {
		return 0;
	}

	src->type = SYS_TYPE_CHILD;
	src->d.child.pid = pid;
	log_debug(3, "sys_child_watch(%d) => tag=%u", pid, src->tag);
	return src->tag;
}


sys_tag_t sys_quit_handler(sys_func_t func, void *arg)
{
	sys_source_t *src = sys_source_add(func, arg);

	if (src == NULL) {
		return 0;
	}

	src->type = SYS_TYPE_QUIT;
	log_debug(3, "sys_quit_handler() => tag=%u", src->tag);
	return src->tag;
}


static int sys_callback(sys_source_t *src)
{
	int ret = 0;

	if (src->func != NULL) {
		switch (src->type) {
		case SYS_TYPE_IO:
			if (src->d.io.poll) {
				ret = ((sys_poll_func_t) src->func)(src->arg, &src->d.io.pollfd);
			}
			else {
				ret = ((sys_io_func_t) src->func)(src->arg, src->d.io.pollfd.fd);
			}
			break;
		case SYS_TYPE_CHILD:
			ret = ((sys_child_func_t) src->func)(src->arg, src->d.child.pid, src->d.child.status);
			break;
		default:
			ret = src->func(src->arg);
			break;
		}
	}

	if (ret == 0) {
		sys_source_clear(src);
	}

	return ret;
}


static void sys_waitpid(void)
{
	pid_t pid;
	int status;
	int i;

	log_debug(4, "(sys_waitpid)");

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		log_debug(3, "sys_waitpid => pid=%d status=%d", pid, status);

		for (i = 0; i < NSOURCES; i++) {
			sys_source_t *src = &sources[i];

			if (src->type == SYS_TYPE_CHILD) {
				if (src->d.child.pid == pid) {
					src->d.child.status = status;
					sys_callback(src);
					sys_source_clear(src);
				}
			}
		}
	}

	if ((pid < 0) && (errno != ECHILD)) {
		log_str("PANIC: waitpid: %s", strerror(errno));
	}
}


void sys_run(void)
{
	int i;

	log_debug(1, "Entering processing loop");

	while (quit_requested == 0) {
		unsigned long long now;
		struct pollfd fds[NSOURCES];
		int fds_lookup[NSOURCES];
		int nfds = 0;
		long long timeout = -1;

		now = sys_now();
		if (now == 0) {
			break;
		}

		/* Check timer events */
		for (i = 0; i < NSOURCES; i++) {
			sys_source_t *src = &sources[i];

			if (src->type == SYS_TYPE_TIMEOUT) {
				if (src->d.timeout.t <= now) {
					/* Timer expired => invoke timeout callback */
					if (sys_callback(src)) {
						if (src->type == SYS_TYPE_TIMEOUT) {
							src->d.timeout.t = now + src->d.timeout.delay;
						}
					}
				}
			}
		}

		/* Construct poll settings */
		for (i = 0; i < NSOURCES; i++) {
			sys_source_t *src = &sources[i];


			fds_lookup[i] = -1;

			if (src->type == SYS_TYPE_TIMEOUT) {
				log_debug(4, "sys_run/1: TIMEOUT tag=%u %llu %llu", src->tag, src->d.timeout.t, now);
				if (src->d.timeout.t > now) {
					unsigned long long dt = src->d.timeout.t - now;
					if ((timeout < 0) || (dt < timeout)) {
						timeout = dt;
					}
				}
			}

			else if (src->type == SYS_TYPE_IO) {
				log_debug(4, "sys_run/1: IO tag=%u", src->tag);
				src->d.io.pollfd.revents = 0;
				fds_lookup[i] = nfds;
				fds[nfds] = src->d.io.pollfd;
				nfds++;
			}

			else if (src->type == SYS_TYPE_REMOVED) {
				log_debug(4, "sys_run/1: REMOVED tag=%u", src->tag);
				sys_source_clear(src);
			}
		}

		/* Wait for something to happen */
		log_debug(4, "sys_run/2: poll (timeout=%ld)", timeout);
		int status = poll(fds, nfds, timeout);
		log_debug(4, "sys_run/3: poll => status=%d", status);

		if (status < 0) {
			if ((errno != EAGAIN) && (errno != EINTR)) {
				log_str("PANIC: select: %s", strerror(errno));
				break;
			}
		}
		else if (status > 0) {
			/* Check io events */
			for (i = 0; i < NSOURCES; i++) {
				sys_source_t *src = &sources[i];
				int fdsi = fds_lookup[i];

				if ((src->type == SYS_TYPE_IO) && (fdsi >= 0)) {
					unsigned int revents = fds[fdsi].revents;
					log_debug(4, "sys_run/4: IO tag=%d fd=%d revents=%02X", src->tag, src->d.io.pollfd.fd, revents);
					if (revents != 0) {
						src->d.io.pollfd.revents = revents;
						sys_callback(src);
					}
				}
			}
		}

		/* Check for SIGCHLD events */
		sys_waitpid();
	}

	log_debug(1, "Leaving processing loop");

	if (quit_requested) {
		for (i = 0; i < NSOURCES; i++) {
			sys_source_t *src = &sources[i];

			if (src->type == SYS_TYPE_QUIT) {
				sys_callback(src);
				sys_source_clear(src);
			}
		}
	}
}


void sys_quit(void)
{
	log_debug(3, "sys_quit");
	quit_requested = 1;
}


static void sys_quit_signal(int sig)
{
	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);

	quit_requested = 1;
}


static void sys_sigchld(int sig)
{
	/* Child death will be acknowledged by waitpid(),
	   as this signal will cause select() to be interrupted (EAGAIN) */
}


int sys_init(void)
{
	if (opt_daemon) {
		signal(SIGHUP, SIG_IGN);
	}
	else {
		signal(SIGHUP, sys_quit_signal);
	}
	signal(SIGINT, sys_quit_signal);
	signal(SIGQUIT, sys_quit_signal);
	signal(SIGTERM, sys_quit_signal);
	signal(SIGCHLD, sys_sigchld);
	signal(SIGPIPE, SIG_IGN);
	return 0;
}
