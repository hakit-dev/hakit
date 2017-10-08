/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2016 Sylvain Giroudon
 *
 * Piped subprocess
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/types.h>

#include "log.h"
#include "env.h"
#include "proc.h"
#include "mod.h"
#include "str_argv.h"

#include "version.h"


#define CLASS_NAME "proc"

typedef enum {
	MODE_COMMAND=0,
	MODE_AGENT,
} proc_mode_t;

typedef struct {
	hk_obj_t *obj;
	int cmd_argc;
	char **cmd_argv;
	proc_mode_t mode;
	int respawn_delay;
	sys_tag_t respawn_tag;
	hk_pad_t *pad_enable;
	hk_pad_t *pad_in;
	hk_pad_t *pad_run;
	hk_pad_t *pad_out;
	hk_pad_t *pad_status;
	hk_proc_t *proc;
} ctx_t;

static void _term(ctx_t *ctx, int status);


static int _new(hk_obj_t *obj)
{
	ctx_t *ctx;
	char *str;

	/* Get command */
	str = hk_prop_get(&obj->props, "cmd");
	if (str == NULL) {
		log_str("ERROR: %s: No command defined", obj->name, str);
		return -1;
	}

	/* Create object context */
	ctx = malloc(sizeof(ctx_t));
	memset(ctx, 0, sizeof(ctx_t));
	ctx->obj = obj;
	obj->ctx = ctx;

	/* Get command line */
	ctx->cmd_argc = str_argv(str, &ctx->cmd_argv);

	/* Get start/kill mode */
	str = hk_prop_get(&obj->props, "mode");
	if (str != NULL) {
		if (strcmp(str, "command") == 0) {
			ctx->mode = MODE_COMMAND;
		}
		else if (strcmp(str, "agent") == 0) {
			ctx->mode = MODE_AGENT;
		}
		else {
			log_str("ERROR: %s: Unknown start mode '%s'", obj->name, str);
			return -1;
		}
	}

	/* Get respawn timer */
	str = hk_prop_get(&obj->props, "respawn");
	if (str != NULL) {
		ctx->respawn_delay = atoi(str);
	}

	/* Create input/output pads */
	ctx->pad_enable = hk_pad_create(obj, HK_PAD_IN, "enable");
	ctx->pad_in = hk_pad_create(obj, HK_PAD_IN, "in");
	ctx->pad_run = hk_pad_create(obj, HK_PAD_OUT, "run");
	ctx->pad_out = hk_pad_create(obj, HK_PAD_OUT, "out");
	ctx->pad_status = hk_pad_create(obj, HK_PAD_OUT, "status");

	return 0;
}


static void _stdout(ctx_t *ctx, char *buf, int len)
{
	int i = 0;

	while (i < len) {
		char *str = &buf[i];
		while ((i < len) && (buf[i] != '\n')) {
			i++;
		}
		if (i < len) {
			buf[i++] = '\0';
		}

		log_debug(1, "%s[stdout]: %s", ctx->obj->name, str);
		hk_pad_update_str(ctx->pad_out, str);
	}
}


static void _start(ctx_t *ctx)
{
	if (ctx->proc == NULL) {
		int argc = ctx->cmd_argc;
		char *argv[argc+1];
		int i;

		for (i = 0; i < argc; i++) {
			char *args = ctx->cmd_argv[i];
			if (args[0] == '$') {
				char *s = hk_pad_get_value(ctx->obj, args+1);
				if (s != NULL) {
					args = s;
				}
				else {
					log_str("WARNING: %s: Referencing unknown pad '%s' in command line argument $%d", ctx->obj->name, args+1, i);
				}
			}

			argv[i] = args;
		}

		argv[i] = NULL;

                char *dir = env_appdir(NULL);
		ctx->proc = hk_proc_start(argv, dir,
					  (hk_proc_out_func_t) _stdout, NULL,
					  (hk_proc_term_func_t) _term, ctx);

                if (dir != NULL) {
                        free(dir);
                }

		if (ctx->proc != NULL) {
			hk_pad_update_int(ctx->pad_run, 1);
		}
	}
}


static int _respawn(ctx_t *ctx)
{
	ctx->respawn_tag = 0;

	if (ctx->pad_enable->state) {
		_start(ctx);
	}

	return 0;
}


static void _term(ctx_t *ctx, int status)
{
	log_debug(1, "%s: terminated with status=%d", ctx->obj->name, status);
	ctx->proc = NULL;
	hk_pad_update_int(ctx->pad_run, 0);
	hk_pad_update_int(ctx->pad_status, status);

	if (ctx->respawn_delay > 0) {
		log_debug(1, "%s: respawning process in %d seconds...", ctx->obj->name, ctx->respawn_delay);
		if (ctx->respawn_tag != 0) {
			sys_remove(ctx->respawn_tag);
		}
		ctx->respawn_tag = sys_timeout(ctx->respawn_delay * 1000, (sys_func_t) _respawn, ctx);
	}
}


static void _enable(ctx_t *ctx, int value)
{
	if (value != ctx->pad_enable->state) {
		log_debug(1, "%s: enable=%d", ctx->obj->name, value);
		ctx->pad_enable->state = value;

		if (value) {
			_start(ctx);
		}
		else {
			/* Kill running respwan timer, if any */
			if (ctx->respawn_tag != 0) {
				sys_remove(ctx->respawn_tag);
				ctx->respawn_tag = 0;
			}

			/* Kill process */
			if ((ctx->proc != NULL) && (ctx->mode == MODE_AGENT)) {
				hk_proc_stop(ctx->proc);
			}
		}
	}
}


static void _in(ctx_t *ctx, char *value)
{
	if (ctx->proc != NULL) {
		log_debug(1, "%s: write to stdin: \"%s\"", ctx->obj->name, value);
		hk_proc_write(ctx->proc, value, strlen(value));
		hk_proc_write(ctx->proc, "\n", 1);
	}
}


static void _input(hk_pad_t *pad, char *value)
{
	ctx_t *ctx = pad->obj->ctx;

	if (pad == ctx->pad_enable) {
		_enable(ctx, atoi(value));
	}
	else if (pad == ctx->pad_in) {
		_in(ctx, value);
	}
}


hk_class_t _class = {
	.name = CLASS_NAME,
	.version = VERSION,
	.new = _new,
	.input = _input,
};
