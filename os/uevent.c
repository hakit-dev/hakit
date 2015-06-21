#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/types.h>
#include <linux/netlink.h>

#include "log.h"
#include "sys.h"
#include "uevent.h"

/* Disable uevent if not supported by kernel */
#ifdef NETLINK_KOBJECT_UEVENT
#define HAKIT_UEVENT_ENABLED

static sys_tag_t uevent_tag = 0;

static uevent_hook_t *uevent_hooks = NULL;
static int uevent_nhooks = 0;
#endif


#ifdef HAKIT_UEVENT_ENABLED

static int uevent_event(void *arg, int fd)
{
	uevent_data_t d;
	char *action = NULL;
	char *subsystem = NULL;
	int i;

	d.size = recv(fd, &d.buf, sizeof(d.buf), 0);
	if (d.size < 0) {
		log_str("ERROR: uevent recv: %s", strerror(errno));
		close(fd);
		return 0;
	}

	log_debug(3, "uevent_event: %d bytes received", d.size);
	i = 0;
	while (i < d.size) {
		char *str = d.buf + i;
		int len = strlen(str);
		log_debug(3, "  %s", str);

		if (strncmp(str, "ACTION=", 7) == 0) {
			action = str + 7;
		}
		else if (strncmp(str, "SUBSYSTEM=", 10) == 0) {
			subsystem = str + 10;
		}

		i += len + 1;
	}

	log_debug(2, "uevent_event: action='%s' subsystem='%s'", action, subsystem);

	for (i = 0; i < uevent_nhooks; i++) {
		uevent_hook_t *hook = &uevent_hooks[i];

		if (hook->action != NULL) {
			if ((action == NULL) || strcmp(action, hook->action)) {
				continue;
			}
		}

		if (hook->subsystem != NULL) {
			if ((subsystem == NULL) || strcmp(subsystem, hook->subsystem)) {
				continue;
			}
		}

		if (hook->func != NULL) {
			hook->func(hook->user_data, &d);
		}
	}

	return 1;
}

#endif /* HAKIT_UEVENT_ENABLED */


static int uevent_init(void)
{
#ifdef HAKIT_UEVENT_ENABLED
	int sock;
	int buffersize;
	struct sockaddr_nl snl;

	/* Do nothing if uevent listening is already initialised */
	if (uevent_tag > 0) {
		return 0;
	}

	sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT); 
	if (sock == -1) {
		log_str("ERROR: uevent socket: %s", strerror(errno));
		return -1;
	}
	
	/* We're trying to override buffer size. If we fail, we attempt to set a big buffer and pray */
	buffersize = 16 * 1024 * 1024;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUFFORCE, &buffersize, sizeof(buffersize))) {
		/* Somewhat safe default. */
		buffersize = 106496;
		
		if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buffersize, sizeof(buffersize))) {
			close(sock);
			log_str("ERROR: uevent setsockopt: %s", strerror(errno));
			return -1;
		}
	}

	memset(&snl, 0x00, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();
	snl.nl_groups = 1;

	if (bind(sock, (struct sockaddr *) &snl, sizeof(struct sockaddr_nl))) {
		close(sock);
		log_str("ERROR: uevent bind: %s", strerror(errno));
		return -1;
	}

	uevent_tag = sys_io_watch(sock, uevent_event, NULL);

	return 0;
#else
	log_str("ERROR: Netlink uevent not supported by this kernel");
	return -1;
#endif /* HAKIT_UEVENT_ENABLED */
}


int uevent_add(char *action, char *subsystem, uevent_func_t func, void *user_data)
{
#ifdef HAKIT_UEVENT_ENABLED
	uevent_hook_t *hook;

	/* Init uevent gears.
	   This will have no effect if init was already done before */
	uevent_init();

	log_debug(2, "uevent_add: action='%s' subsystem='%s'", action, subsystem);

	/* Allocate new hook */
	uevent_nhooks++;
	uevent_hooks = realloc(uevent_hooks, uevent_nhooks * sizeof(uevent_hook_t));
	hook = &uevent_hooks[uevent_nhooks-1];

	hook->func = func;
	hook->user_data = user_data;

	if (action != NULL) {
		hook->action = strdup(action);
	}
	else {
		hook->action = NULL;
	}

	if (subsystem != NULL) {
		hook->subsystem = strdup(subsystem);
	}
	else {
		hook->subsystem = NULL;
	}

	return 0;
#else
	return -1;
#endif /* HAKIT_UEVENT_ENABLED */
}


char *uevent_getenv(uevent_data_t *d, char *env)
{
#ifdef HAKIT_UEVENT_ENABLED
	char env_str[strlen(env)+2];
	int env_len;
	int i;

	env_len = snprintf(env_str, sizeof(env_str), "%s=", env);

	i = 0;
	while (i < d->size) {
		char *str = d->buf + i;
		int len = strlen(str);

		if (strncmp(str, env_str, env_len) == 0) {
			return str + env_len;
		}

		i += len + 1;
	}
#endif /* HAKIT_UEVENT_ENABLED */

	return NULL;
}
