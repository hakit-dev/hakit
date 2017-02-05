/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * HTTP/WebSockets authentication
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libwebsockets.h>
#include <sys/stat.h>

#include "log.h"
#include "tab.h"
#include "ws_auth.h"

typedef struct {
	char *name;
	char *auth;
} ws_auth_user_t;


static int auth_required = 0;

static HK_TAB_DECLARE(users, ws_auth_user_t);
#define USER_ENTRY(i) HK_TAB_PTR(users, ws_auth_user_t, i)


static int check_permissions(char *file)
{
	struct stat st;

	if (stat(file, &st) < 0) {
		log_str("WARNING: %s: %s", file, strerror(errno));
		return -1;
	}

	if ((st.st_mode & (S_IRWXG|S_IRWXO)) != 0) {
		log_str("WARNING: %s: Wrong permissions - Please deny access to group and others", file);
		return -1;
	}

	return 0;
}


int ws_auth_init(char *auth_file)
{
	FILE *f;
	int lineno = 0;
	char buf[256];

	auth_required = 1;

	/* Check auth file has acceptable permissions */
	if (check_permissions(auth_file) != 0) {
		return -1;
	}

	/* Fetch user list */
	f = fopen(auth_file, "r");
	if (f == NULL) {
		log_str("ERROR: %s: %s", auth_file, strerror(errno));
		return -1;
	}

	while (fgets(buf, sizeof(buf), f) != NULL) {
		lineno++;

		/* Strip leading blanks */
		char *s1 = buf;
		while ((*s1 != '\0') && (*s1 <= ' ')) {
			s1++;
		}

		/* Cut-off comments */
		char *s2 = strchr(s1, '#');
		if (s2 != NULL) {
			*s2 = '\0';
		}

		/* Strip trailing blanks */
		int len = strlen(s1);
		while ((len > 0) && (s1[len-1] <= ' ')) {
			len--;
			s1[len] = '\0';
		}

		/* Ignore empty lines */
		if (*s1 == '\0') {
			continue;
		}

		/* Ensure user definition format is username:password */
		s2 = strchr(s1, ':');
		if (s2 == NULL) {
			log_str("WARNING: %s:%d: Badly formatted user definition -- ignored", auth_file, lineno);
			continue;
		}

		ws_auth_user_t *user = hk_tab_push(&users);
		int size = len*2 + 3;
		user->auth = malloc(size);
		lws_b64_encode_string(s1, len, user->auth, size);

		*s2 = '\0';
		user->name = strdup(s1);

		log_debug(2, "ws_auth_init: user '%s' added", user->name);
	}

	fclose(f);

	return 0;
}


int ws_auth_check(struct lws *wsi, char **username)
{
	int len;

	if (!auth_required) {
		return 1;
	}
	
	len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_AUTHORIZATION);
	if (len > 0) {
		char str[len+1];

		lws_hdr_copy(wsi, str, sizeof(str), WSI_TOKEN_HTTP_AUTHORIZATION);
		if (strncmp(str, "Basic ", 6) == 0) {
			char *auth = str + 6;
			int i;

			for (i = 0; i < users.nmemb; i++) {
				ws_auth_user_t *user = USER_ENTRY(i);
				if (strcmp(auth, user->auth) == 0) {
					if (username != NULL) {
						*username = user->name;
					}
					return 1;
				}
			}
		}
	}

	return 0;
}
