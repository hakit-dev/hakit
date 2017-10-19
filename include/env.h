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

/**
 * @brief Init environment probing
 * @param argc
 *   Number of command line arguments (as given to main()).
 * @param argv
 *   Array of command line arguments (as given to main()).
 */
extern void env_init(int argc, char *argv[]);

extern char *env_devdir(char *subpath);
extern char *env_bindir(char *subpath);

/**
 * @brief Search program file in PATH directories
 * @param pgm
 *   Program file to search
 * @return
 *   Full path of the program file.
 *   Allocated with malloc(). Must be free'd after usage to avoid memory leaks.
 *   NULL if program is not found.
 */
extern char *env_which(char *pgm);

#endif /* __HAKIT_ENV_H__ */
