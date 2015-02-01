#ifndef __HAKIT_PROC_H__
#define __HAKIT_PROC_H__

#include <unistd.h>

#include "sys.h"

typedef struct {
	pid_t pid;
	int fd_in;
	int fd_out;
	int fd_err;
	sys_tag_t sigchld_tag;
} hakit_proc_t;

#endif /* __HAKIT_PROC_H__ */
