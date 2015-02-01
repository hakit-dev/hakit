#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include "tcpio.h"

typedef void (* http_server_handler_t)(tcp_sock_t *client, char *location);

typedef struct {
	char *location;
	int len;
	http_server_handler_t handler;
} http_server_alias_t;

typedef struct {
	tcp_srv_t server;
	char *document_root;
	http_server_alias_t *aliases;
	int naliases;
} http_server_t;

extern http_server_t *http_server_new(int port, char *document_root);
extern void http_server_alias(http_server_t *http, char *location, http_server_handler_t handler);

extern int http_send_file(tcp_sock_t *client, char *fname);
extern void http_send_buf(tcp_sock_t *client, char *ctype, buf_t *cbuf);

#endif /* __HTTP_SERVER_H__ */
