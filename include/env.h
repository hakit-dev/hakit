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

#ifndef __HAKIT_ENV_H__
#define __HAKIT_ENV_H__

extern void env_init(int argc, char *argv[]);
extern char *env_devdir(char *subpath);
extern char *env_bindir(char *subpath);

extern char *env_app(void);
extern char *env_appdir(char *subpath);

#endif /* __HAKIT_ENV_H__ */
