/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Execution environment probing
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>


static int env_devel_ = 0;


void env_init(int argc, char *argv[])
{
	char *bindir = dirname(argv[0]);
	char path[strlen(bindir)+64];

	snprintf(path, sizeof(path), "%s/classes/Makefile", bindir);

	if (access(path, R_OK) == 0) {
		env_devel_ = 1;
	}
}


int env_devel(void)
{
	return env_devel_;
}
