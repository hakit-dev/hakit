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
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <libgen.h>
#include <unistd.h>

#include "env.h"


static char *env_curdir_ = NULL;
static char *env_bindir_ = NULL;
static char *env_devdir_ = NULL;


char *env_which(char *pgm)
{
	int pgmlen = strlen(pgm);
	char *_path_ = getenv("PATH");
	if (_path_ == NULL) {
		return NULL;
	}
	_path_ = strdup(_path_);

	char *dir = _path_;
	char *ret = NULL;
	while ((dir != NULL) && (ret == NULL)) {
		char *sep = strchr(dir, ':');
		if (sep != NULL) {
			*(sep++) = '\0';
		}

		{
			char path[strlen(dir)+pgmlen+2];
			snprintf(path, sizeof(path), "%s/%s", dir, pgm);
			if (access(path, X_OK) == 0) {
				ret = realpath(dir, NULL);
			}
		}

		dir = sep;
	}

	free(_path_);

	return ret;
}


void env_init(int argc, char *argv[])
{
	char *path;

	/* Get current directory */
	if (env_curdir_ == NULL) {
                char path[1024];
                env_curdir_ = strdup(getcwd(path, sizeof(path)));
        }

	/* Probe for HAKit development environment */
	if (env_bindir_ != NULL) {
		free(env_bindir_);
	}
	path = strdup(argv[0]);
	if (strchr(path, '/') != NULL) {
		env_bindir_ = realpath(dirname(path), NULL);
	}
	else {
		env_bindir_ = env_which(argv[0]);
	}
	free(path);

	/* Probe for development environment */
	if (env_devdir_ != NULL) {
		free(env_devdir_);
		env_devdir_ = NULL;
	}

        char *dir = env_bindir(".version");
        if (dir != NULL) {
                if (access(dir, R_OK) == 0) {
                        path = strdup(env_bindir_);
                        if (path != NULL) {
                                env_devdir_ = strdup(dirname(dirname(path)));
                                free(path);
                        }
                }
                free(dir);
        }
}


static char *env_dir(char *rootdir, char *subpath)
{
	char *path;
        int size;
        int len;

	if (rootdir == NULL) {
		rootdir = env_curdir_;
	}

        /* Compute path length */
        size = strlen(rootdir)+4;
        if (subpath != NULL) {
                size += strlen(subpath);
        }

        /* Alloc path buffer */
        path = malloc(size);
        if (path != NULL) {
                len = snprintf(path, size, "%s", rootdir);
                if (subpath != NULL) {
                        len += snprintf(path+len, size-len, "/%s", subpath);
                }
        }

	return path;
}


char *env_bindir(char *subpath)
{
	return env_dir(env_bindir_, subpath);
}


char *env_devdir(char *subpath)
{
	/* Return NULL if no development environment has been detected */
	if (env_devdir_ == NULL) {
		return NULL;
	}

	return env_dir(env_devdir_, subpath);
}
