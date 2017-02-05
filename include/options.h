/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2015 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_OPTIONS_H__
#define __HAKIT_OPTIONS_H__

#ifdef __cplusplus
extern "C" {
#endif

#define OPTIONS_DEFAULT_CONF "/etc/hakit/config"
#define OPTIONS_DEFAULT_AUTH "/etc/hakit/users"

#define OPTIONS_TYPE_NONE 0
#define OPTIONS_TYPE_INT 1
#define OPTIONS_TYPE_STRING 2

#define OPTIONS_ENTRY_NULL { NULL, '\0', 0, OPTIONS_TYPE_NONE, NULL, NULL, NULL }

typedef struct {
	const char *long_opt;
	const char short_opt;
	const int x;
	const int type;
	const void *value_ptr;
	const char *description;
	const char *value_symbol;
} options_entry_t;

extern const char *options_summary;
extern const options_entry_t options_entries[];

extern int opt_debug;
extern int opt_daemon;

extern int options_parse(int *_argc, char *argv[]);
extern void options_usage(void);

#ifdef __cplusplus
}
#endif

#endif /* __HAKIT_OPTIONS_H__ */
