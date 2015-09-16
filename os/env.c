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
#include <malloc.h>
#include <libgen.h>
#include <unistd.h>

#include "env.h"


static char *env_bindir_ = NULL;
static char *env_devdir_ = NULL;
static char *env_appdir_ = NULL;
static char *env_app_ = NULL;


void env_init(int argc, char *argv[])
{
	char *path;

	/* Probe for HAKit development environment */
	if (env_bindir_ != NULL) {
		free(env_bindir_);
	}
	path = strdup(argv[0]);
	env_bindir_ = strdup(dirname(path));
	free(path);

	/* Probe for development environment */
	if (env_devdir_ != NULL) {
		free(env_devdir_);
		env_devdir_ = NULL;
	}
	if (access(env_bindir(".version"), R_OK) == 0) {
		path = strdup(env_bindir_);
		env_devdir_ = strdup(dirname(dirname(env_bindir_)));
		free(path);
	}

	/* Retrieve HAKit application path */
	if (env_app_ != NULL) {
		free(env_app_);
		env_app_ = NULL;
	}
	if (env_appdir_ != NULL) {
		free(env_appdir_);
		env_appdir_ = NULL;
	}

	if (argc > 1) {
		env_app_ = strdup(argv[1]);

		path = strdup(env_app_);
		env_appdir_ = strdup(dirname(path));
		free(path);
	}
}


static char *env_dir(char *rootdir, char *subpath)
{
	static char *path = NULL;

	if (rootdir == NULL) {
		rootdir = ".";
	}

	if (path != NULL) {
		free(path);
		path = NULL;
	}

	{
		int size = strlen(rootdir)+strlen(subpath)+4;
		path = malloc(size);
		snprintf(path, size, "%s/%s", rootdir, subpath);
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


char *env_app(void)
{
	return env_app_;
}


char *env_appdir(char *subpath)
{
	/* Return NULL if no application is loaded */
	if (env_app_ == NULL) {
		return NULL;
	}

	return env_dir(env_appdir_, subpath);
}
