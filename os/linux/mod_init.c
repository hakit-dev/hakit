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

#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>

#include "types.h"
#include "env.h"
#include "log.h"
#include "files.h"
#include "mod.h"
#include "hakit_version.h"
#include "mod_init.h"


static void hk_mod_init_dir(char *dir)
{
	int dirlen = strlen(dir);
	DIR *d;
	struct dirent *ent;

	d = opendir(dir);
	if (d == NULL) {
		log_debug(2, "Directory '%s' not found", dir);
		return;
	}

	log_str("Scanning class directory '%s' ...", dir);

	while ((ent = readdir(d)) != NULL) {
		char *name = ent->d_name;
		if (name[0] != '.') {
                        int namelen = strlen(name);
			char path[dirlen + namelen*2 + 32];

			/* Class should be stored in a directory */
			snprintf(path, sizeof(path), "%s/%s", dir, name);
			if (!is_dir(path)) {
				continue;
			}

			/* Open device library */
                        snprintf(path, sizeof(path), "%s/%s/device/%s.so", dir, name, name);
                        if (!is_file(path)) {
                                continue;
                        }
                        log_debug(3, "  %s: %s", name, path);

                        void *dl = dlopen(path, RTLD_LAZY);
			if (dl != NULL) {
                                char varname[namelen+8];
                                snprintf(varname, sizeof(varname), "_class_%s", name);
                                char *s = varname;
                                while (*s != '\0') {
                                        if (*s == '-') {
                                                *s = '_';
                                        }
                                        s++;
                                }

				hk_class_t *class = dlsym(dl, varname);
                                if (class == NULL) {
                                        log_debug(3, "Class '%s': symbol '%s' not found, trying '_class'", varname);
                                        class = dlsym(dl, "_class");
                                }

				if (class != NULL) {
					if (hk_class_register(class)) {
                                                dlclose(dl);
                                        }
                                        else {
                                                log_str("%s: Class '%s' registered (%s)", path, class->name, class->version ? class->version:"");
                                                // Keep library open
                                        }
				}
				else {
					log_str("Class '%s': %s", name, dlerror());
					dlclose(dl);
				}
			}
			else {
				log_str("Class '%s': %s", name, dlerror());
			}
		}
	}

	closedir(d);
}


void hk_mod_init(char *class_path)
{
	char *s1 = class_path;

	while (s1 != NULL) {
		char *s2 = strchr(s1, ':');
		if (s2 == NULL) {
			s2 = strchr(s1, ',');
		}
		if (s2 != NULL) {
			*(s2++) = '\0';
		}

		hk_mod_init_dir(s1);

		s1 = s2;
	}

        char *dir = env_bindir("classes");
        if (dir != NULL) {
                /* Don't care if this directory does not exist */
                if (is_dir(dir)) {
                        hk_mod_init_dir(dir);
                }
                free(dir);
        }

        hk_mod_init_dir("/usr/lib/hakit/classes");
}
