/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2016 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_WS_CLIENT_H__
#define __HAKIT_WS_CLIENT_H__

typedef void ws_client_func_t(void *user_data, char *buf, int len);

typedef struct {
	void *context;
	int use_ssl;
} ws_client_t;

extern int ws_client_init(ws_client_t *client, int use_ssl);
extern void ws_client_destroy(ws_client_t *client);

extern int ws_client_get(ws_client_t *client, char *uri, char *headers,
			 ws_client_func_t *func, void *user_data);

#endif /* __HAKIT_WS_CLIENT_H__ */
