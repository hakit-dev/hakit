#ifndef __HAKIT_COMMAND_H__
#define __HAKIT_COMMAND_H__

#include "buf.h"
#include "io.h"

typedef void (*command_handler_t)(void *user_data, int argc, char **argv);

typedef struct {
	buf_t line;
	command_handler_t handler;
	void *user_data;
} command_t;

extern command_t *command_new(command_handler_t handler, void *user_data);
extern void command_destroy(command_t *cmd);
extern void command_clear(command_t *cmd);

extern int command_recv(command_t *cmd, char *buf, int len);

#endif /* __HAKIT_COMMAND_H__ */
