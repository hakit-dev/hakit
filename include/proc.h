/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Subprocess management
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_PROC_H__
#define __HAKIT_PROC_H__

#include "sys.h"
#include "io.h"

typedef void (*hk_proc_out_func_t)(void *user_data, char *buf, int size);
typedef void (*hk_proc_term_func_t)(void *user_data, int status);

typedef enum {
	HK_PROC_ST_FREE=0,
	HK_PROC_ST_RUN,
	HK_PROC_ST_KILL,
} hk_proc_state_t;

typedef struct {
	hk_proc_state_t state;
	pid_t pid;
	int stdin_fd;
	io_channel_t stdout;
	io_channel_t stderr;
	sys_tag_t sigchld_tag;
	sys_tag_t timeout_tag;
	hk_proc_out_func_t cb_stdout;
	hk_proc_out_func_t cb_stderr;
	hk_proc_term_func_t cb_term;
	void *user_data;
} hk_proc_t;


extern hk_proc_t *hk_proc_start(char *argv[], char *cwd,
				hk_proc_out_func_t cb_stdout,
				hk_proc_out_func_t cb_stderr,
				hk_proc_term_func_t cb_term,
				void *user_data);

extern void hk_proc_stop(hk_proc_t * proc);

extern int hk_proc_write(hk_proc_t * proc, char *buf, int size);
extern int hk_proc_printf(hk_proc_t * proc, char *fmt, ...);


#endif /* __HAKIT_PROC_H__ */
