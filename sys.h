#ifndef __SYS_H__
#define __SYS_H__

#include <sys/types.h>

typedef unsigned int sys_tag_t;
typedef int (*sys_func_t)(void *arg);
typedef int (*sys_io_func_t)(void *arg, int fd);
typedef int (*sys_child_func_t)(void *arg, pid_t pid, int status);

extern int sys_init(void);

extern sys_tag_t sys_io_watch(int fd, sys_io_func_t func, void *arg);
extern sys_tag_t sys_timeout(unsigned long delay, sys_func_t func, void *arg);
extern sys_tag_t sys_child_watch(pid_t pid, sys_child_func_t func, void *arg);
extern sys_tag_t sys_quit_handler(sys_func_t func, void *arg);

extern void sys_remove(sys_tag_t tag);

extern void sys_run(void);
extern void sys_quit(void);

#endif /* __SYS_H__ */
