#ifndef __HAKIT_IO_H__
#define __HAKIT_IO_H__

#include "sys.h"

#define IO_CHANNEL_NULL {-1, 0, NULL, NULL}

typedef void (*io_func_t)(void *user_data, char *buf, int len);

typedef struct {
	int fd;
	sys_tag_t tag;
	io_func_t func;
	void *user_data;
} io_channel_t;

extern int io_blocking(int fd, int blocking);

extern void io_channel_setup(io_channel_t *chan, int fd, io_func_t func, void *user_data);
extern void io_channel_setup_io(io_channel_t *chan, int fd, sys_io_func_t io_func, void *user_data);
extern void io_channel_clear(io_channel_t *chan);
static inline int io_channel_fd(io_channel_t *chan) { return chan->fd; }

extern void io_channel_close(io_channel_t *chan);
extern int io_channel_write(io_channel_t *chan, char *buf, int len);
extern int io_channel_write_async(io_channel_t *chan, char *buf, int len);

#endif /* __HAKIT_IO_H__ */
