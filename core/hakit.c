/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "types.h"
#include "options.h"
#include "sys.h"
#include "log.h"
#include "comm.h"
#include "mod.h"
#include "mod_init.h"


//===================================================
// Command line arguments
//===================================================

const char *options_summary = "HAKit " xstr(HAKIT_VERSION) " (" xstr(ARCH) ")";

const options_entry_t options_entries[] = {
	{ "debug",  'd', 0, OPTIONS_TYPE_INT,  &opt_debug,  "Set debug level", "N" },
	{ "daemon", 'D', 0, OPTIONS_TYPE_NONE, &opt_daemon, "Run in background as a daemon" },
	{ NULL }
};


static void run_as_daemon(void)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		log_str("ERROR: Fork failed: %s", strerror(errno));
		exit(3);
	}


	if (pid > 0) {
		exit(0);
	}

	if (setsid() < 0) {
		log_str("ERROR: Setsid failed: %s", strerror(errno));
		exit(3);
 	}

	close(STDIN_FILENO);
}


int main(int argc, char *argv[])
{
	if (options_parse(&argc, argv, NULL) != 0) {
		exit(1);
	}

	if (opt_daemon) {
		run_as_daemon();
	}

	/* Init log management */
	log_init("hakit");
	log_str(options_summary);

	/* Init system runtime */
	sys_init();

	/* Init communication engine */
	if (comm_init()) {
		return 2;
	}

	/* Init module management */
	if (hk_mod_init()) {
		return 2;
	}

	sys_run();

	return 0;
}
