#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>

#include "sys.h"
#include "log.h"
#include "io.h"
#include "command.h"


int command_parse(char *line, char ***_argv)
{
	int argc = 0;
	char **argv = malloc(sizeof(char *));

	argv[argc] = NULL;

	while (*line != '\0') {
		while ((*line != '\0') && (*line <= ' ')) {
			line++;
		}
		if (*line != '\0') {
			argv = realloc(argv, (argc+2) * sizeof(char *));
			argv[argc++] = line;
			argv[argc] = NULL;

			while (*line > ' ') {
				line++;
			}
			if (*line != '\0') {
				*(line++) = '\0';
			}
		}
	}

	*_argv = argv;

	return argc;
}


int command_recv(command_t *cmd, char *buf, int len)
{
	int ret = 1;

	if (buf == NULL) {
		if (cmd->handler != NULL) {
			cmd->handler(NULL, cmd->arg);
		}
		ret = 0;
	}
	else {
		int i = 0;

		while (i < len) {
			while ((i < len) && (buf[i] != '\n')) {
				i++;
			}

			buf_append(&cmd->line, (unsigned char *) buf, i);

			if (i < len) {
				if (cmd->handler != NULL) {
					cmd->handler((char *) cmd->line.base, cmd->arg);
				}

				cmd->line.len = 0;
				i++;
			}
		}
	}

	return ret;
}


command_t *command_new(command_handler_t handler, void *arg)
{
	command_t *cmd = malloc(sizeof(command_t));
	memset(cmd, 0, sizeof(command_t));

	buf_init(&cmd->line);
	cmd->handler = handler;
	cmd->arg = arg;

	return cmd;
}


void command_destroy(command_t *cmd)
{
	buf_cleanup(&cmd->line);
	memset(cmd, 0, sizeof(command_t));
	free(cmd);
}


void command_clear(command_t *cmd)
{
	buf_cleanup(&cmd->line);
}
