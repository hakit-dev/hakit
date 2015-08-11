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
#include "mod.h"
#include "hakit_version.h"


static int hk_mod_init_dir(char *dir)
{
	int dirlen = strlen(dir);
	DIR *d;
	struct dirent *ent;

	log_str("Scanning class directory '%s' ...", dir);

	d = opendir(dir);
	if (d == NULL) {
		log_debug(2, "Directory '%s' not found", dir);
		return 0;
	}

	while ((ent = readdir(d)) != NULL) {
		char *name = ent->d_name;
		if (name[0] != '.') {
			char path[dirlen + strlen(name)*2 + 32];

			/* Class should be stored in a directory */
			snprintf(path, sizeof(path), "%s/%s", dir, name);
			DIR *d = opendir(path);
			if (d == NULL) {
				continue;
			}
			closedir(d);

			/* Compute device library file name */
			snprintf(path, sizeof(path), "%s/%s/device/" ARCH "/%s.so", dir, name, name);

			/* Open device library */
			void *dl = dlopen(path, RTLD_LAZY);
			if (dl != NULL) {
				hk_class_t *class = dlsym(dl, "_class");
				if (class != NULL) {
					hk_class_register(class);
					log_str("%s: Class '%s' registered (%s)", path, class->name, class->version ? class->version:"");
					// Keep library open
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

	return 0;
}


int hk_mod_init(char *class_path)
{
	char *s1 = class_path;
	int ret = 0;

	while ((s1 != NULL) && (ret == 0)) {
		char *s2 = strchr(s1, ':');
		if (s2 == NULL) {
			s2 = strchr(s1, ',');
		}
		if (s2 != NULL) {
			*(s2++) = '\0';
		}

		ret = hk_mod_init_dir(s1);

		s1 = s2;
	}

	if (ret == 0) {
		if (env_devel()) {
			ret = hk_mod_init_dir("classes");
		}
	}

	if (ret == 0) {
		ret = hk_mod_init_dir("/usr/lib/hakit");
	}

	return ret;
}
