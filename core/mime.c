/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2015 Sylvain Giroudon
 *
 * Mime types for the HTTP server
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>

#include "mime.h"

typedef struct {
	const char *suffix;
	const char *type;
} ws_mime_t;

static const ws_mime_t ws_mimes[] = {
	{"ico",  "image/x-icon"},
	{"png",  "image/png"},
	{"jpg",  "image/jpeg"},
	{"svg",  "image/svg+xml"},
	{"html", "text/html"},
	{"js",   "text/javascript"},
	{"css",  "text/css"},
	{NULL, NULL}
};


const char *get_mimetype(const char *file)
{
	char *suffix = strrchr(file, '.');
	const ws_mime_t *mime = ws_mimes;

	if (suffix != NULL) {
		suffix++;
		while (mime->suffix != NULL) {
			if (strcmp(suffix, mime->suffix) == 0) {
				return mime->type;
			}
			mime++;
		}
	}

	return NULL;
}
