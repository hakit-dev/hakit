/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_SYS_H__
#define __HAKIT_SYS_H__

#include <sys/types.h>
#include <poll.h>

typedef unsigned int sys_tag_t;
typedef int (*sys_func_t)(void *arg);
typedef int (*sys_io_func_t)(void *arg, int fd);
typedef int (*sys_poll_func_t)(void *arg, struct pollfd *pollfd);
typedef int (*sys_child_func_t)(void *arg, pid_t pid, int status);

extern int sys_init(void);

extern sys_tag_t sys_io_watch(int fd, sys_io_func_t func, void *arg);
extern sys_tag_t sys_io_poll(int fd, unsigned int events, sys_poll_func_t func, void *arg);
extern sys_tag_t sys_timeout(unsigned long delay, sys_func_t func, void *arg);
extern sys_tag_t sys_child_watch(pid_t pid, sys_child_func_t func, void *arg);
extern sys_tag_t sys_quit_handler(sys_func_t func, void *arg);

extern void sys_remove(sys_tag_t tag);
extern void sys_remove_fd(int fd);

extern void sys_run(void);
extern void sys_quit(void);

#endif /* __HAKIT_SYS_H__ */
