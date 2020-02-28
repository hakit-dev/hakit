/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_TCPIO_H__
#define __HAKIT_TCPIO_H__

#include <netinet/in.h>

#include "buf.h"
#include "io.h"

/*
 * SSL context
 */
#ifdef WITH_SSL
#include <openssl/ssl.h>
#endif /* WITH_SSL */


/*
 * TCP socket
 */

typedef enum {
	TCP_IO_CONNECT=0,
	TCP_IO_DATA,
	TCP_IO_HUP
} tcp_io_t;

typedef struct tcp_sock_s tcp_sock_t;

typedef void (* tcp_func_t)(tcp_sock_t *tcp_sock, tcp_io_t io, char *rbuf, int rsize);

struct tcp_sock_s {
	io_channel_t chan;
	tcp_func_t func;
	void *user_data;
        buf_t wbuf;
#ifdef WITH_SSL
        SSL_CTX *ssl_ctx;
        SSL *ssl;
#endif
};

extern void tcp_sock_clear(tcp_sock_t *tcp_sock);
extern void tcp_sock_set_data(tcp_sock_t *tcp_sock, void *user_data);
extern void *tcp_sock_get_data(tcp_sock_t *tcp_sock);

extern int tcp_sock_connect(tcp_sock_t *tcp_sock, char *host, int port, char *certs,
			    tcp_func_t func, void *user_data);
extern int tcp_sock_is_connected(tcp_sock_t *tcp_sock);
extern void tcp_sock_write(tcp_sock_t *tcp_sock, char *buf, int size);
extern void tcp_sock_shutdown(tcp_sock_t *tcp_sock);


/*
 * TCP Server
 */

#define TCP_SRV_MAX_CLIENTS 32

typedef struct {
	tcp_sock_t csock;
	tcp_sock_t dsock[TCP_SRV_MAX_CLIENTS];
	struct sockaddr_in iremote;
	tcp_func_t func;
	void *user_data;
} tcp_srv_t;

extern void tcp_srv_clear(tcp_srv_t *srv);
extern int tcp_srv_init(tcp_srv_t *srv, int port, char *certs, tcp_func_t func, void *user_data);
extern void tcp_srv_shutdown(tcp_srv_t *srv);

typedef int (* tcp_foreach_func_t)(tcp_sock_t *tcp_sock, void *user_data);
extern void tcp_srv_foreach_client(tcp_srv_t *srv, tcp_foreach_func_t func, void *user_data);

#endif /* __HAKIT_TCPIO_H__ */
