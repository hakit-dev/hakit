#ifndef __COMMAND_H__
#define __COMMAND_H__

#include "buf.h"
#include "io.h"

typedef void (*command_handler_t)(char *line, void *arg);

typedef struct {
	buf_t line;
	command_handler_t handler;
	void *arg;
} command_t;

extern command_t *command_new(command_handler_t handler, void *arg);
extern void command_destroy(command_t *cmd);
extern void command_clear(command_t *cmd);

extern int command_recv(command_t *cmd, char *buf, int len);
extern int command_parse(char *line, char ***_argv);

#endif /* __COMMAND_H__ */
