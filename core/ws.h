/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_WS_H__
#define __HAKIT_WS_H__

#include "buf.h"
#include "tab.h"

typedef struct ws_s ws_t;
typedef void (*ws_handler_t)(ws_t *ws, char *uri, buf_t *buf);

typedef struct {
	char *location;
	int len;
	ws_handler_t handler;
} ws_alias_t;

struct ws_s {
	void *context;
	char *document_root;
	int document_root_len;
	hk_tab_t aliases;       // Table of (ws_alias_t)
};


extern ws_t *ws_new(int port, char *document_root);
extern void ws_destroy(ws_t *ws);

extern void ws_alias(ws_t *ws, char *location, ws_handler_t handler);

#endif /* __HAKIT_WS_H__ */
