/*
 * HAKit - The Home Automation KIT
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <dirent.h>

int is_dir(char *path)
{
	DIR *d = opendir(path);
	if (d == NULL) {
		return 0;
	}

	closedir(d);
	return 1;
}


int is_file(char *path)
{
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return 0;
	}

	fclose(f);
	return 1;
}
