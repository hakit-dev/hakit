/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "log.h"
#include "str_argv.h"
#include "command.h"


int command_recv(command_t *cmd, char *buf, int len)
{
	int ret = 1;

	if (buf == NULL) {
		if (cmd->handler != NULL) {
			cmd->handler(cmd->user_data, 0, NULL);
		}
		ret = 0;
	}
	else {
		int i = 0;

		while (i < len) {
			int i0 = i;

			while ((i < len) && (buf[i] != '\n')) {
				i++;
			}

			buf_append(&cmd->line, (unsigned char *) &buf[i0], i-i0);

			if (i < len) {
				log_debug(2, "command_recv: '%s'", cmd->line.base);
				if ((cmd->handler != NULL) && (cmd->line.len > 0)) {
					char **argv = NULL;
					int argc = str_argv((char *) cmd->line.base, &argv);

					cmd->handler(cmd->user_data, argc, argv);

					if (argv != NULL) {
						free(argv);
					}
				}

				cmd->line.len = 0;
				i++;
			}
		}
	}

	return ret;
}


command_t *command_new(command_handler_t handler, void *user_data)
{
	command_t *cmd = malloc(sizeof(command_t));
	memset(cmd, 0, sizeof(command_t));

	buf_init(&cmd->line);
	cmd->handler = handler;
	cmd->user_data = user_data;

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
