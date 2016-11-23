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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include "log.h"
#include "sys.h"
#include "proc.h"


static hk_proc_t *procs = NULL;
static int nprocs = 0;


static hk_proc_t *hk_proc_find_free(void)
{
	int i;

	/* Find a free entry in proc table */
	for (i = 0; i < nprocs; i++) {
		if (procs[i].state == HK_PROC_ST_FREE) {
			return &procs[i];
		}
	}

	return NULL;
}


static hk_proc_t *hk_proc_add(void)
{
	/* Find a free entry in proc table */
	hk_proc_t *proc = hk_proc_find_free();

	/* If none found, allocate a new one */
	if (proc == NULL) {
		nprocs++;
		procs = (hk_proc_t *) realloc(procs, nprocs * sizeof(hk_proc_t));
		proc = &procs[nprocs-1];
	}

	memset(proc, 0, sizeof(hk_proc_t));
	return proc;
}


static void hk_proc_remove(hk_proc_t *proc)
{
	proc->state = HK_PROC_ST_FREE;
}


static void hk_proc_timeout_cancel(hk_proc_t *proc)
{
	if (proc->timeout_tag) {
		sys_remove(proc->timeout_tag);
		proc->timeout_tag = 0;
	}
}


static void hk_proc_term(hk_proc_t *proc)
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

	hk_proc_timeout_cancel(proc);

	if (proc->stdin_fd > 0) {
		close(proc->stdin_fd);
		proc->stdin_fd = 0;
	}
}


static int hk_proc_hangup_timeout(hk_proc_t *proc)
{
	log_debug(2, "hk_proc_hangup_timeout [%d]", proc->pid);
	proc->timeout_tag = 0;
	hk_proc_stop(proc);
	return 0;
}


static void hk_proc_print_buf(hk_proc_t *proc, char *tag, char *buf, int len)
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
		log_str("[%d] %s: %s", proc->pid, tag, str);
	}
}


static void hk_proc_stdout(hk_proc_t *proc, char *buf, int len)
{
	log_debug(2, "hk_proc_stdout [%d] len=%d", proc->pid, len);

	if (len > 0) {
		if (proc->cb_stdout != NULL) {
			proc->cb_stdout(proc->user_data, buf, len);
		}
		else {
			hk_proc_print_buf(proc, "stdout", buf, len);
		}
	}
	else {
		hk_proc_timeout_cancel(proc);
		proc->timeout_tag = sys_timeout(100, (sys_func_t) hk_proc_hangup_timeout, proc);
	}
}


static void hk_proc_stderr(hk_proc_t *proc, char *buf, int len)
{
	log_debug(2, "hk_proc_stderr [%d] len=%d", proc->pid, len);

	if (len > 0) {
		if (proc->cb_stderr != NULL) {
			proc->cb_stderr(proc->user_data, buf, len);
		}
		else {
			hk_proc_print_buf(proc, "stderr", buf, len);
		}
	}
}


static int hk_proc_sigchld(hk_proc_t *proc, pid_t pid, int status)
{
	log_debug(2, "hk_proc_sigchld [%d] status=%d", pid, status);

	if (pid == proc->pid) {
		proc->pid = 0;

		/* Cancel kill timeout */
		hk_proc_timeout_cancel(proc);

		if (proc->cb_term != NULL) {
			proc->cb_term(proc->user_data, status);
		}

		hk_proc_term(proc);
		hk_proc_remove(proc);
	}
	else {
		log_str("WARNING: Caught SIGCHLD from unknown process (pid=%d)", pid);
	}

	return 0;
}


hk_proc_t *hk_proc_start(int argc, char *argv[], char *cwd,
			 hk_proc_out_func_t cb_stdout,
			 hk_proc_out_func_t cb_stderr,
			 hk_proc_term_func_t cb_term,
			 void *user_data)
{
	hk_proc_t *proc = NULL;
	int p_in[2] = {-1,-1};
	int p_out[2] = {-1,-1};
	int p_err[2] = {-1,-1};
	pid_t pid;

	if (argc < 1) {
		log_str("PANIC: Cannot start process with empty command line");
		return NULL;
	}

	log_debug(2, "hk_proc_start %s ...", argv[0]);
	log_debug(2, "hk_proc_start chdir = %s", cwd);

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
		/* Change working directory, if provided */
		if (cwd != NULL) {
			if (chdir(cwd) == 0) {
				log_debug(2, "hk_proc_start: chdir to '%s'", cwd);
			}
			else {
				log_str("WARNING: Cannot chdir to '%s': %s", cwd, strerror(errno));
			}
		}

		/* Check access to command */
		if (access(argv[0], X_OK) == -1) {
			log_str("ERROR: Cannot access proc command '%s': %s", argv[0], strerror(errno));
			exit(254);
		}

		/* Redirect stdin to input pipe */
		if (dup2(p_in[0], STDIN_FILENO) == -1) {
			log_str("ERROR: Cannot dup2 proc stdin: %s", strerror(errno));
			exit(254);
		}
		close(p_in[0]);
		close(p_in[1]);

		/* Redirect stdout to output pipe */
		if (dup2(p_out[1], STDOUT_FILENO) == -1) {
			log_str("ERROR: Cannot dup2 proc stdout: %s", strerror(errno));
			exit(254);
		}
		close(p_out[0]);
		close(p_out[1]);

		/* Redirect stderr to error pipe */
		if (dup2(p_err[1], STDERR_FILENO) == -1) {
			log_str("ERROR: Cannot dup2 proc stderr: %s", strerror(errno));
			exit(254);
		}
		close(p_err[0]);
		close(p_err[1]);

		/* Perform exec (returnless call if success) */
		execvp(argv[0], argv);

		/* Return from exec: something went wrong */
		fprintf(stderr, "ERROR: execvp(%s): %s (pid=%d): %s\n", argv[0], getpid(), strerror(errno));
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

		proc = hk_proc_add();
		proc->pid = pid;
		close(p_in[0]);
		proc->stdin_fd = p_in[1];
		close(p_out[1]);
		close(p_err[1]);

		/* Hook stdio handler */
		io_channel_setup(&proc->stdout, p_out[0], (io_func_t) hk_proc_stdout, proc);
		io_channel_setup(&proc->stderr, p_err[0], (io_func_t) hk_proc_stderr, proc);

		/* Hook sigchld handler */
		proc->sigchld_tag = sys_child_watch(pid, (sys_child_func_t) hk_proc_sigchld, proc);

		/* Enable close-on-exec mode on local pipe endpoints */
		fcntl(proc->stdin_fd, F_SETFD, FD_CLOEXEC);
		fcntl(proc->stdout.fd, F_SETFD, FD_CLOEXEC);
		fcntl(proc->stderr.fd, F_SETFD, FD_CLOEXEC);

		/* Setup callbacks */
		proc->cb_stdout = cb_stdout;
		proc->cb_stderr = cb_stderr;
		proc->cb_term = cb_term;
		proc->user_data = user_data;

		proc->state = HK_PROC_ST_RUN;

		break;
	}

	return proc;
}


static int hk_proc_kill_timeout(hk_proc_t * proc)
{
	proc->timeout_tag = 0;

	log_str("WARNING: Process pid=%d takes too long to terminate - Killing it", proc->pid);
	kill(proc->pid, SIGKILL);

	hk_proc_term(proc);
	hk_proc_remove(proc);

	return 0;
}


void hk_proc_stop(hk_proc_t * proc)
{
	if (proc->state == HK_PROC_ST_RUN) {
		log_debug(2, "Sending process pid=%d the TERM signal", proc->pid);
		proc->state = HK_PROC_ST_KILL;

		if (kill(proc->pid, SIGTERM) == 0) {
			/* Start kill timeout */
			proc->timeout_tag = sys_timeout(1000, (sys_func_t) hk_proc_kill_timeout, proc);
		}
		else {
			log_debug(2, "  => %s", strerror(errno));

			if (proc->cb_term != NULL) {
				proc->cb_term(proc->user_data, -1);
			}

			hk_proc_term(proc);
			hk_proc_remove(proc);
		}
	}
}


int hk_proc_write(hk_proc_t * proc, char *buf, int size)
{
	if (proc->stdin_fd < 0) {
		log_str("WARNING: Writing to closed process input (pid=%d)", proc->pid);
		return 0;
	}

	return write(proc->stdin_fd, buf, size);
}


int hk_proc_printf(hk_proc_t * proc, char *fmt, ...)
{
	va_list ap;
	char buf[1024];
	int size;

	va_start(ap, fmt);
	size = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (size <= 0) {
		return size;
	}

	return hk_proc_write(proc, buf, size);
}
