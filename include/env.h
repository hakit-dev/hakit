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

#ifndef __HK_ENV_H__
#define __HK_ENV_H__

extern void env_init(int argc, char *argv[]);
extern int env_devel(void);
extern char *env_bindir(char *subpath);

#endif /* __HK_ENV_H__ */
