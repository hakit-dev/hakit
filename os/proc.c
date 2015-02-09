#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include "log.h"
#include "sys.h"
#include "proc.h"


static hakit_proc_t *procs = NULL;
static int nprocs = 0;


static hakit_proc_t *proc_find_free(void)
{
	int i;

	/* Find a free entry in proc table */
	for (i = 0; i < nprocs; i++) {
		if (procs[i].state == HAKIT_PROC_ST_FREE) {
			return &procs[i];
		}
	}

	return NULL;
}


static hakit_proc_t *proc_add(void)
{
	/* Find a free entry in proc table */
	hakit_proc_t *proc = proc_find_free();

	/* If none found, allocate a new one */
	if (proc == NULL) {
		nprocs++;
		procs = (hakit_proc_t *) realloc(procs, nprocs * sizeof(hakit_proc_t));
		proc = &procs[nprocs-1];
	}

	memset(proc, 0, sizeof(hakit_proc_t));
	return proc;
}


static void proc_remove(hakit_proc_t *proc)
{
	proc->state = HAKIT_PROC_ST_FREE;
}


static void proc_timeout_cancel(hakit_proc_t *proc)
{
	if (proc->timeout_tag) {
		sys_remove(proc->timeout_tag);
		proc->timeout_tag = 0;
	}
}


static void proc_term(hakit_proc_t *proc)
{
	proc->cb_stdout = NULL;
	proc->cb_stderr = NULL;
	proc->cb_term = NULL;

	io_channel_close(&proc->stdout);
	io_channel_close(&proc->stderr);

	if (proc->sigchld_tag) {
		sys_remove(proc->sigchld_tag);
		proc->sigchld_tag = 0;
	}

	proc_timeout_cancel(proc);

	if (proc->stdin_fd > 0) {
		close(proc->stdin_fd);
		proc->stdin_fd = 0;
	}
}


static int proc_hangup_timeout(hakit_proc_t *proc)
{
	log_debug(2, "proc_hangup_timeout");
	proc_stop(proc);
	return 0;
}


static int proc_stdout(hakit_proc_t *proc, char *buf, int len)
{
	log_debug(2, "proc_stdout len=%d", len);

	if (len > 0) {
		if (proc->cb_stdout != NULL) {
			proc->cb_stdout(proc->user_data, buf, len);
		}
	}
	else if (len == 0) {
		proc_timeout_cancel(proc);
		proc->timeout_tag = sys_timeout(100, (sys_func_t) proc_hangup_timeout, proc);
	}

	return 1;
}


static int proc_stderr(hakit_proc_t *proc, char *buf, int len)
{
	log_debug(2, "proc_stderr len=%d", len);

	if (len > 0) {
		if (proc->cb_stderr != NULL) {
			proc->cb_stderr(proc->user_data, buf, len);
		}
	}

	return 1;
}


static int proc_sigchld(hakit_proc_t *proc, pid_t pid, int status)
{
	log_debug(2, "proc_sigchld pid=%d status=%d", pid, status);

	if (pid == proc->pid) {
		proc->pid = 0;

		/* Cancel kill timeout */
		proc_timeout_cancel(proc);

		if (proc->cb_term != NULL) {
			proc->cb_term(proc->user_data, status);
		}

		proc_term(proc);
		proc_remove(proc);
	}
	else {
		log_str("WARNING: Caught SIGCHLD from unknown process (pid=%d)", pid);
	}

	return 0;
}


hakit_proc_t *proc_start(int argc, char *argv[],
			 proc_out_func_t cb_stdout,
			 proc_out_func_t cb_stderr,
			 proc_term_func_t cb_term,
			 void *user_data)
{
	hakit_proc_t *proc = NULL;
	int p_in[2] = {-1,-1};
	int p_out[2] = {-1,-1};
	int p_err[2] = {-1,-1};
	pid_t pid;

	if (argc < 1) {
		log_str("PANIC: Cannot start process with empty command line");
		return NULL;
	}

	log_debug(2, "proc_start %s ...", argv[0]);

	/* Check access to command */
	if (access(argv[0], X_OK) == -1) {
		log_str("ERROR: Cannot access proc command '%s': %s", argv[0], strerror(errno));
		return NULL;
	}

	/* Create standard input pipe */
	if (pipe(p_in) == -1) {
		log_str("ERROR: Cannot create proc stdin pipe: %s", strerror(errno));
		return NULL;
	}

	/* Create standard output pipe */
	if ( pipe(p_out) == -1 ) {
		log_str("ERROR: Cannot create proc stdout pipe: %s", strerror(errno));
		close(p_in[0]);
		close(p_in[1]);
		return NULL;
	}

	/* Create standard error pipe */
	if ( pipe(p_err) == -1 ) {
		log_str("ERROR: Cannot create proc stderr pipe: %s", strerror(errno));
		close(p_in[0]);
		close(p_in[1]);
		close(p_out[0]);
		close(p_out[1]);
		return NULL;
	}

	pid = fork();
	switch (pid) {
	case 0: /* Child */
		if (dup2(p_in[0], STDIN_FILENO) == -1) {
			log_str("ERROR: Cannot dup2 proc stdin: %s", strerror(errno));
			exit(254);
		}
		close(p_in[0]);
		close(p_in[1]);

		if (dup2(p_out[1], STDOUT_FILENO) == -1) {
			log_str("ERROR: Cannot dup2 proc stdout: %s", strerror(errno));
			exit(254);
		}
		close(p_out[0]);
		close(p_out[1]);

		if (dup2(p_err[1], STDERR_FILENO) == -1) {
			log_str("ERROR: Cannot dup2 proc stderr: %s", strerror(errno));
			exit(254);
		}
		close(p_err[0]);
		close(p_err[1]);

		/* Perform exec (returnless call if success) */
		execvp(argv[0], argv);

		/* Return from exec: something went wrong */
		log_str("ERROR: execvp(%s): %s (pid=%d): %s\n", argv[0], getpid(), strerror(errno));
		exit(255);
		break;

	case -1: /* Error */
		log_str("PANIC: Cannot fork process: %s", strerror(errno));
		close(p_in[0]);
		close(p_in[1]);
		close(p_out[0]);
		close(p_out[1]);
		close(p_err[0]);
		close(p_err[1]);
		break;

	default : /* Parent */
		log_debug(2, "  => pid=%d", pid);

		proc = proc_add();
		proc->pid = pid;
		close(p_in[0]);
		proc->stdin_fd = p_in[1];
		close(p_out[1]);
		close(p_err[1]);

		/* Hook stdio handler */
		io_channel_setup(&proc->stdout, p_out[0], (io_func_t) proc_stdout, proc);
		io_channel_setup(&proc->stderr, p_err[0], (io_func_t) proc_stderr, proc);

		/* Hook sigchld handler */
		proc->sigchld_tag = sys_child_watch(pid, (sys_child_func_t) proc_sigchld, proc);

		/* Enable close-on-exec mode on local pipe endpoints */
		fcntl(proc->stdin_fd, F_SETFD, FD_CLOEXEC);
		fcntl(proc->stdout.fd, F_SETFD, FD_CLOEXEC);
		fcntl(proc->stderr.fd, F_SETFD, FD_CLOEXEC);

		/* Setup callbacks */
		proc->cb_stdout = cb_stdout;
		proc->cb_stderr = cb_stderr;
		proc->cb_term = cb_term;
		proc->user_data = user_data;

		proc->state = HAKIT_PROC_ST_RUN;

		break;
	}

	return proc;
}


static int proc_kill_timeout(hakit_proc_t * proc)
{
	proc->timeout_tag = 0;

	log_str("WARNING: Process pid=%s takes too long to terminate - Killing it", proc->pid);
	kill(proc->pid, SIGKILL);

	proc_term(proc);
	proc_remove(proc);

	return 0;
}


void proc_stop(hakit_proc_t * proc)
{
	//log_debug(2, "proc_stop pid=%d state=%d", proc->pid, proc->state);

	if (proc->state == HAKIT_PROC_ST_RUN) {
		log_debug(2, "Sending process pid=%d the TERM signal", proc->pid);
		kill(proc->pid, SIGTERM);
		proc->state = HAKIT_PROC_ST_KILL;

		/* Start kill timeout */
		proc->timeout_tag = sys_timeout(1000, (sys_func_t) proc_kill_timeout, proc);
	}
}
