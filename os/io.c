#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "log.h"
#include "sys.h"
#include "io.h"


int io_blocking(int fd, int blocking)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL, 0)) == -1) {
		log_str("ERROR: fcntl(%d, F_GETFL): %s", fd, strerror(errno));
		return -1;
	}

	if (blocking) {
		flags &= ~O_NONBLOCK;
	}
	else {
		flags |= O_NONBLOCK;
	}

	if (fcntl(fd, F_SETFL, flags) == -1) {
		log_str("ERROR: fcntl(%d, F_SETFL): %s", fd, strerror(errno));
		return -1;
	}

	return 0;
}


static int io_channel_event(io_channel_t *chan, int fd)
{
	int ret = 1;
	char buf[BUFSIZ+1];
	int len;

	while ((len = read(fd, buf, sizeof(buf)-1)) > 0) {
		buf[len] = 0;
		if (chan->func != NULL) {
			chan->func(chan->user_data, buf, len);
		}
	}

	if (len < 0) {
		if ((errno != EAGAIN) && (errno != EINTR)) {
			log_str("ERROR: io_channel_event [%d]: %s", fd, strerror(errno));
			ret = 0;
		}
	}
	else if (len == 0) {
		log_debug(3, "io_channel_event [%d] HANGUP", fd);
		ret = 0;
	}

	/* Process HANGUP event */
	if (ret == 0) {
		if (chan->func != NULL) {
			chan->func(chan->user_data, NULL, 0);
		}
	}

	return ret;
}


void io_channel_setup(io_channel_t *chan, int fd, io_func_t func, void *user_data)
{
	chan->fd = fd;
	chan->tag = sys_io_watch(fd, (sys_io_func_t) io_channel_event, chan);
	chan->func = func;
	chan->user_data = user_data;

	io_blocking(fd, 0);
}


void io_channel_setup_io(io_channel_t *chan, int fd, sys_io_func_t io_func, void *user_data)
{
	chan->fd = fd;
	chan->tag = sys_io_watch(fd, io_func, user_data);

	io_blocking(fd, 0);
}


void io_channel_clear(io_channel_t *chan)
{
	chan->fd = -1;
	chan->tag = 0;
	chan->func = NULL;
	chan->user_data = NULL;
}


void io_channel_close(io_channel_t *chan)
{
	if (chan->tag) {
		sys_remove(chan->tag);
	}
	if (chan->fd >= 0) {
		close(chan->fd);
	}
	io_channel_clear(chan);
}


int io_channel_write(io_channel_t *chan, char *buf, int len)
{
	int ret;

	if (chan->fd < 0)
		return 0;
	if (len <= 0)
		return 0;

	io_blocking(chan->fd, 1);

	ret = write(chan->fd, buf, len);
	if (ret < 0) {
		log_str("ERROR: Socket [%d] write error: %s", chan->fd, strerror(errno));
	}

	io_blocking(chan->fd, 0);

	return ret;
}


int io_channel_write_async(io_channel_t *chan, char *buf, int len)
{
	int ret;

	if (chan->fd < 0)
		return 0;
	if (len <= 0)
		return 0;

	ret = write(chan->fd, buf, len);
	if (ret < 0) {
		log_str("ERROR: Socket [%d] write error: %s", chan->fd, strerror(errno));
	}

	return ret;
}
