#ifndef __HAKIT_PROC_H__
#define __HAKIT_PROC_H__

#include "sys.h"
#include "io.h"

typedef void (*proc_out_func_t)(void *user_data, char *buf, int size);
typedef void (*proc_term_func_t)(void *user_data, int status);

typedef enum {
	HAKIT_PROC_ST_FREE=0,
	HAKIT_PROC_ST_RUN,
	HAKIT_PROC_ST_KILL,
} hakit_proc_state_t;

typedef struct {
	hakit_proc_state_t state;
	pid_t pid;
	int stdin_fd;
	io_channel_t stdout;
	io_channel_t stderr;
	sys_tag_t sigchld_tag;
	sys_tag_t timeout_tag;
	proc_out_func_t cb_stdout;
	proc_out_func_t cb_stderr;
	proc_term_func_t cb_term;
	void *user_data;
} hakit_proc_t;


extern hakit_proc_t *proc_start(int argc, char *argv[],
				proc_out_func_t cb_stdout,
				proc_out_func_t cb_stderr,
				proc_term_func_t cb_term,
				void *user_data);

extern void proc_stop(hakit_proc_t * proc);

extern int proc_write(hakit_proc_t * proc, char *buf, int size);
extern int proc_printf(hakit_proc_t * proc, char *fmt, ...);


#endif /* __HAKIT_PROC_H__ */
