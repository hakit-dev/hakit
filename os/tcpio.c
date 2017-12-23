/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>

#include "log.h"
#include "sys.h"
#include "io.h"
#include "iputils.h"
#include "tcpio.h"

static void tcp_sock_read_event(tcp_sock_t *tcp_sock, char *buf, int len);


/*
 * SSL gears
 */

#ifdef WITH_SSL

#include <openssl/ssl.h>
#include <openssl/err.h>


static int tcp_certificate_verify(int preverify_ok, X509_STORE_CTX *ctx)
{
	/* Preverify should have already checked expiry, revocation.
	 * We may need to verify other settings here (hostname, ...) */
        log_debug(3, "tcp_certificate_verify %d", preverify_ok);
        return preverify_ok;
}


static SSL_CTX *tcp_sock_ssl_ctx(char *certs, int server)
{
        static int tcp_ssl_initialized = 0;
        SSL_CTX *ssl_ctx;

        log_debug(3, "tcp_sock_ssl_ctx certs='%s' server=%d", certs, server);

        /* Check arguments */
        if (certs == NULL) {
                return NULL;
        }

        /* Init SSL lib */
        if (!tcp_ssl_initialized) {
                SSL_library_init();
                SSL_load_error_strings();
                OpenSSL_add_all_algorithms();
                tcp_ssl_initialized = 1;
        }

        int len = strlen(certs);
        char cafile[len+16];
        snprintf(cafile, sizeof(cafile), "%s/ca.crt", certs);
        char certfile[len+16];
        snprintf(certfile, sizeof(certfile), "%s/server.crt", certs);
        char keyfile[len+16];
        snprintf(keyfile, sizeof(keyfile), "%s/server.key", certs);

        /* Create SSL server context */
        ssl_ctx = SSL_CTX_new(server ? SSLv23_server_method() : SSLv23_client_method());
        if (ssl_ctx == NULL) {
                log_str("ERROR: SSL: Failed to initialize SSL");
                goto FAILED;
        }

        /* Setup SSL certificate check */
        if (!SSL_CTX_load_verify_locations(ssl_ctx, cafile, NULL)) {
                log_str("ERROR: SSL: Failed to load CA certificate '%s'", cafile);
                goto FAILED;
        }
        log_debug(3, "tcp_sock_ssl_ctx: CA file loaded: %s", cafile);

        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, tcp_certificate_verify);

        /* Setup SSL key and cert */
        if (!SSL_CTX_use_certificate_file(ssl_ctx, certfile, SSL_FILETYPE_PEM)) {
                log_str("ERROR: SSL: Failed to load cert file '%s'", certfile);
                goto FAILED;
        }
        log_debug(3, "tcp_sock_ssl_ctx: cert file loaded: %s", certfile);

        if (!SSL_CTX_use_PrivateKey_file(ssl_ctx, keyfile, SSL_FILETYPE_PEM)) {
                log_str("ERROR: SSL: Failed to load key file '%s'", keyfile);
                goto FAILED;
        }
        log_debug(3, "tcp_sock_ssl_ctx: key file loaded: %s", keyfile);

        if (!SSL_CTX_check_private_key(ssl_ctx)) {
                log_str("ERROR: SSL: Certificate file '%s' and key file '%s' are inconsistent", certfile, keyfile);
                goto FAILED;
        }
        log_debug(3, "tcp_sock_ssl_ctx: cert/key verification passed");

        return ssl_ctx;

FAILED:
        if (ssl_ctx != NULL) {
                SSL_CTX_free(ssl_ctx);
                ssl_ctx = NULL;
        }

        return NULL;
}


static int tcp_sock_ssl_setup(tcp_sock_t *tcp_sock, int sock, SSL_CTX *ssl_ctx, int server)
{
        log_debug(3, "tcp_sock_ssl_setup [%d] server=%d", sock, server);

        /* Setup SSL gears */
        tcp_sock->ssl = SSL_new(ssl_ctx);
        if (tcp_sock->ssl == NULL) {
                long error = ERR_get_error();
                const char* error_str = ERR_error_string(error, NULL);
                log_str("ERROR: Could not init SSL on server connection [%d]: %s", sock, error_str);
                goto FAILED;
        }

        if (!SSL_set_fd(tcp_sock->ssl, sock)) {
                log_str("ERROR: Could not setup SSL on server connection [%d]", sock);
                goto FAILED;
        }

        /* Init SSL state */
        if (server) {
                SSL_set_accept_state(tcp_sock->ssl);
        }
        else {
                SSL_set_connect_state(tcp_sock->ssl);
        }

        return 0;

FAILED:
        if (tcp_sock->ssl != NULL) {
                SSL_free(tcp_sock->ssl);
                tcp_sock->ssl = NULL;
        }
        return -1;
}


static int tcp_sock_ssl_write_event(tcp_sock_t *tcp_sock, int fd)
{
        int cont = 0;

        log_debug(3, "tcp_sock_ssl_write_event [%d] size=%d", fd, tcp_sock->wbuf.len);

        if (tcp_sock->wbuf.len > 0) {
                int ret = SSL_write(tcp_sock->ssl, tcp_sock->wbuf.base, tcp_sock->wbuf.len);
                log_debug(3, "SSL_write => %d", ret);

                if (ret >= 0) {
                        buf_shift(&tcp_sock->wbuf, ret);

                        if (tcp_sock->wbuf.len > 0) {
                                cont = 1;
                        }
                }
                else {
                        int ssl_error = SSL_get_error(tcp_sock->ssl, ret);
                        if (ssl_error == SSL_ERROR_WANT_WRITE) {
                                log_debug(3, "SSL_write wants write");
                                cont = 1;
                        }
                        else if (ssl_error == SSL_ERROR_WANT_READ) {
                                log_debug(3, "SSL_write wants read");
                        }
                        else {
                                long error = ERR_get_error();
                                const char* error_str = ERR_error_string(error, NULL);
                                log_str("ERROR: could not SSL_write (%d): %s", ret, error_str);
                                tcp_sock_read_event(tcp_sock, NULL, 0);
                        }
                }
        }

        return cont;
}


static int tcp_sock_ssl_read_event(tcp_sock_t *tcp_sock, int fd)
{
	int cont = 1;
        char buffer[1024];
        int ret;

        log_debug(3, "tcp_sock_ssl_read_event [%d]", fd);

        ret = SSL_read(tcp_sock->ssl, buffer, sizeof(buffer));
        log_debug(3, "SSL_read => %d", ret);
        if (ret == 0) {
                cont = 0;
        }
        else if (ret < 0) {
                int ssl_error = SSL_get_error(tcp_sock->ssl, ret);
                if (ssl_error == SSL_ERROR_WANT_WRITE) {
                        log_debug(3, "SSL_read wants write");
                }
                else if (ssl_error == SSL_ERROR_WANT_READ) {
                        log_debug(3, "SSL_read wants read");
                }
                else {
                        cont = 0;
                }
        }
        else {
                tcp_sock_read_event(tcp_sock, buffer, ret);
        }

        if (cont) {
                if (tcp_sock->wbuf.len > 0) {
                        sys_io_write_handler(tcp_sock->chan.tag, (sys_io_func_t) tcp_sock_ssl_write_event);
                }
        }
        else {
                if (ret < 0) {
                        long error = ERR_get_error();
                        const char* error_str = ERR_error_string(error, NULL);
                        log_str("ERROR: could not SSL_read (%d): %s", ret, error_str);
                }
                tcp_sock_read_event(tcp_sock, NULL, 0);
        }

	return cont;
}


static void tcp_sock_ssl_shutdown(tcp_sock_t *tcp_sock)
{
        if (tcp_sock->ssl != NULL) {
                SSL_free(tcp_sock->ssl);
                tcp_sock->ssl = NULL;
        }

        if (tcp_sock->ssl_ctx != NULL) {
                SSL_CTX_free(tcp_sock->ssl_ctx);
                tcp_sock->ssl_ctx = NULL;
        }
}

#else /* WITH_SSL */

static SSL_CTX *tcp_sock_ssl_ctx(char *certs)
{
        if (certs != NULL) {
                log_str("ERROR: TLS/SSL not available");
        }

        return NULL;
}

#endif /* !WITH_SSL */


/*
 * Socket option setup helper
 */

#define KEEPALIVE_IDLE 20
#define KEEPALIVE_INTVL 10
#define KEEPALIVE_CNT 5


static int setsockopt_int(int sock, int level, int optname, int optval)
{
	int ret = setsockopt(sock, level, optname, &optval, sizeof(optval));

	if (ret < 0) {
		log_str("ERROR: setsockopt: %s", strerror(errno));
	}

	return ret;
}


static int setsockopt_keepalive(int sock)
{
	if (setsockopt_int(sock, SOL_SOCKET, SO_KEEPALIVE, 1) < 0) {
		return -1;
	}
	if (setsockopt_int(sock, IPPROTO_TCP, TCP_KEEPIDLE, KEEPALIVE_IDLE)) {
		return -1;
	}
	if (setsockopt_int(sock, IPPROTO_TCP, TCP_KEEPINTVL, KEEPALIVE_INTVL)) {
		return -1;
	}
	if (setsockopt_int(sock, IPPROTO_TCP, TCP_KEEPCNT, KEEPALIVE_CNT)) {
		return -1;
	}

	return 0;
}


/*
 * TCP socket
 */

static void tcp_sock_shutdown_(tcp_sock_t *tcp_sock, int silent)
{
        int fd = io_channel_fd(&tcp_sock->chan);

	if (fd >= 0) {
		if (!silent) {
			log_str("Shutting down connection [%d]", fd);
		}
		shutdown(fd, 2);
        }

        io_channel_close(&tcp_sock->chan);

        buf_cleanup(&tcp_sock->wbuf);

#ifdef WITH_SSL
        tcp_sock_ssl_shutdown(tcp_sock);
#endif
}


void tcp_sock_shutdown(tcp_sock_t *tcp_sock)
{
	tcp_sock_shutdown_(tcp_sock, 0);
}


static void tcp_sock_read_event(tcp_sock_t *tcp_sock, char *buf, int len)
{
	if (tcp_sock->func != NULL) {
		tcp_io_t io = buf ? TCP_IO_DATA : TCP_IO_HUP;
		tcp_sock->func(tcp_sock, io, buf, len);
	}

	if (buf == NULL) {
		log_str("Connection [%d] closed by peer", tcp_sock->chan.fd);
		tcp_sock_shutdown_(tcp_sock, 1);
	}
}


void tcp_sock_clear(tcp_sock_t *tcp_sock)
{
	io_channel_clear(&tcp_sock->chan);
	tcp_sock->func = NULL;
	tcp_sock->user_data = NULL;
}


static void tcp_sock_setup(tcp_sock_t *tcp_sock, int sock, tcp_func_t func, void *user_data)
{
	/* Setup socket keepalive */
	setsockopt_keepalive(sock);

	/* Setup event callback */
#ifdef WITH_SSL
        if (tcp_sock->ssl != NULL) {
                io_channel_setup_io(&tcp_sock->chan, sock, (sys_io_func_t) tcp_sock_ssl_read_event, tcp_sock);
        }
        else
#endif
        {
                io_channel_setup(&tcp_sock->chan, sock, (io_func_t) tcp_sock_read_event, tcp_sock);
        }

	tcp_sock->func = func;
	tcp_sock->user_data = user_data;
}


int tcp_sock_connect(tcp_sock_t *tcp_sock, char *host, int port, char *certs,
                     tcp_func_t func, void *user_data)
{
	struct hostent *hp;
	struct sockaddr_in iremote;
	int sock;
	char s_addr[INET6_ADDRSTRLEN+1];

	log_debug(2, "tcp_sock_connect: host='%s' port=%d", host, port);

	/* Retrieve server address */
	if ((hp = gethostbyname(host)) == NULL ) {
		log_str("ERROR: Unknown host name: %s", host);
		return -1;
	}

	/* Create network socket */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == -1 ) {
		log_str("ERROR: socket: %s", strerror(errno));
		return -1;
	}

	/* Connect to server */
	iremote.sin_family = AF_INET;
	memcpy(&iremote.sin_addr.s_addr, hp->h_addr, hp->h_length);
	iremote.sin_port = htons(port);

	if (connect(sock, (struct sockaddr *) &iremote, sizeof(iremote)) == -1 ) {
		log_str("ERROR: connect(%s:%d): %s", host, port, strerror(errno));
                goto FAILED;
	}

	/* Signal connection */
	ip_addr((struct sockaddr *) &iremote, s_addr, sizeof(s_addr));
	log_str("Outgoing connection [%d] established to %s:%d", sock, s_addr, port);

        /* Setup SSL gears */
        if (certs != NULL) {
                tcp_sock->ssl_ctx = tcp_sock_ssl_ctx(certs, 0);
                if (tcp_sock->ssl_ctx == NULL) {
                        goto FAILED;
                }
#ifdef WITH_SSL
                if (tcp_sock_ssl_setup(tcp_sock, sock, tcp_sock->ssl_ctx, 0) < 0) {
                        goto FAILED;
                }
#endif
        }

	/* Hook an IO watch on this socket */
	tcp_sock_setup(tcp_sock, sock, func, user_data);

	return sock;

FAILED:
        close(sock);
        return -1;
}


int tcp_sock_is_connected(tcp_sock_t *tcp_sock)
{
	if (tcp_sock == NULL)
		return 0;
	return (tcp_sock->chan.fd >= 0);
}


static int tcp_sock_write_event(tcp_sock_t *tcp_sock, int fd)
{
        int cont = 0;

        log_debug(3, "tcp_sock_write_event [%d] size=%d", fd, tcp_sock->wbuf.len);

        if (tcp_sock->wbuf.len > 0) {
                int ret = io_channel_write_async(&tcp_sock->chan, (char *) tcp_sock->wbuf.base, tcp_sock->wbuf.len);
                log_debug(3, "io_channel_write_async => %d", ret);

                if (ret >= 0) {
                        buf_shift(&tcp_sock->wbuf, ret);

                        if (tcp_sock->wbuf.len > 0) {
                                cont = 1;
                        }
                }
                else {
                        tcp_sock_read_event(tcp_sock, NULL, 0);
                }
        }

        log_debug(3, "  => len=%d, cont=%d", tcp_sock->wbuf.len, cont);

        return cont;
}


void tcp_sock_write(tcp_sock_t *tcp_sock, char *buf, int size)
{
	if (tcp_sock != NULL) {
                log_debug(3, "tcp_sock_write [%d] size=%d+%d", tcp_sock->chan.fd, tcp_sock->wbuf.len, size);

                buf_append(&tcp_sock->wbuf, (unsigned char *) buf, size);

                if (tcp_sock->wbuf.len > 0) {
#ifdef WITH_SSL
                        if (tcp_sock->ssl != NULL) {
                                sys_io_write_handler(tcp_sock->chan.tag, (sys_io_func_t) tcp_sock_ssl_write_event);
                        }
                        else
#endif
                        {
                                sys_io_write_handler(tcp_sock->chan.tag, (sys_io_func_t) tcp_sock_write_event);
                        }
                }
        }
}


void tcp_sock_set_data(tcp_sock_t *tcp_sock, void *user_data)
{
	tcp_sock->user_data = user_data;
}


void *tcp_sock_get_data(tcp_sock_t *tcp_sock)
{
	return tcp_sock->user_data;
}


/*
 * TCP Server
 */

static int tcp_srv_csock_accept(tcp_srv_t *srv)
{
	int sock = -1;
	socklen_t size;
	int dnum;
        tcp_sock_t *dsock;
	char s_addr[INET6_ADDRSTRLEN+1];

	/* Accept client connection */
	size = sizeof(srv->iremote);
	sock = accept(srv->csock.chan.fd, (struct sockaddr *) &srv->iremote, &size);
	if (sock < 0) {
		log_str("ERROR: accept [%d]: %s", srv->csock.chan.fd, strerror(errno));
		return -1;
	}

	/* Find an available data socket */
	for (dnum = 0; dnum < TCP_SRV_MAX_CLIENTS; dnum++) {
		if (srv->dsock[dnum].chan.fd < 0)
			break;
	}

	if (dnum >= TCP_SRV_MAX_CLIENTS) {
		log_str("WARNING: Too many connections");
                goto FAILED;
	}

        dsock = &srv->dsock[dnum];

	/* Prevent child processes from inheriting this socket */
	fcntl(sock, F_SETFD, FD_CLOEXEC);

#ifdef WITH_SSL
        /* Setup SSL gears */
        if (srv->csock.ssl_ctx != NULL) {
                if (tcp_sock_ssl_setup(dsock, sock, srv->csock.ssl_ctx, 1) < 0) {
                        goto FAILED;
                }
        }
#endif

	/* Hook an IO watch on this socket */
	tcp_sock_setup(dsock, sock, srv->func, srv->user_data);

	/* Signal connection */
	ip_addr((struct sockaddr *) &srv->iremote, s_addr, sizeof(s_addr));
	log_str("Incomming connection [%d] established from %s:%d", sock, s_addr, ntohs(srv->iremote.sin_port));
	if (srv->func != NULL) {
		srv->func(dsock, TCP_IO_CONNECT, s_addr, strlen(s_addr));
	}

	return 0;

FAILED:
        if (sock >= 0) {
		close(sock);
        }
        return -1;
}


static int tcp_srv_csock_event(tcp_srv_t *srv, int fd)
{
	if (tcp_srv_csock_accept(srv) < 0) {
		log_str("WARNING: Service connection socket shut down");
		log_str("WARNING: Remote clients won't be able to connect any more");
		tcp_srv_shutdown(srv);
		return 0;
	}

	return 1;
}


void tcp_srv_clear(tcp_srv_t *srv)
{
	int i;

	srv->func = NULL;
	srv->user_data = NULL;
	tcp_sock_clear(&srv->csock);

	for (i = 0; i < TCP_SRV_MAX_CLIENTS; i++) {
		tcp_sock_clear(&srv->dsock[i]);
	}
}


int tcp_srv_init(tcp_srv_t *srv, int port, char *certs,
                 tcp_func_t func, void *user_data)
{
	struct sockaddr_in ilocal;

	tcp_srv_clear(srv);

	/* Create network socket */
	if ((srv->csock.chan.fd = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		log_str("ERROR: socket(PF_INET, SOCK_STREAM): %s", strerror(errno));
                goto FAILED;
	}

	setsockopt_int(srv->csock.chan.fd, SOL_SOCKET, SO_REUSEADDR, 1);

	/* Bind network socket */
	ilocal.sin_family = AF_INET;
	ilocal.sin_addr.s_addr = htonl(INADDR_ANY);
	ilocal.sin_port = htons(port);

	if (bind(srv->csock.chan.fd, (struct sockaddr *) &ilocal, sizeof(ilocal)) == -1) { 
		log_str("ERROR: bind(%d): %s", port, strerror(errno));
                goto FAILED;
	}

	/* Listen to network connection (with backlog of maximum 1 pending connection) */
	if (listen(srv->csock.chan.fd, 1) == -1) {
		log_str("ERROR: listen: %s", strerror(errno));
                goto FAILED;
	}
	log_str("Listening to TCP connections from port %d", port);

	/* Prevent child processes from inheriting this socket */
	fcntl(srv->csock.chan.fd, F_SETFD, FD_CLOEXEC);

        /* Hook event handler */
	srv->csock.chan.tag = sys_io_watch(srv->csock.chan.fd, (sys_io_func_t) tcp_srv_csock_event, srv);
	srv->func = func;
	srv->user_data = user_data;

        /* Set SSL mode */
        if (certs != NULL) {
                srv->csock.ssl_ctx = tcp_sock_ssl_ctx(certs, 1);
                if (srv->csock.ssl_ctx == NULL) {
                        goto FAILED;
                }
        }

	return 0;

FAILED:
        io_channel_close(&srv->csock.chan);

        return -1;
}


void tcp_srv_shutdown(tcp_srv_t *srv)
{
	int i;

	srv->func = NULL;
	srv->user_data = NULL;

	for (i = 0; i < TCP_SRV_MAX_CLIENTS; i++)
		tcp_sock_shutdown(&srv->dsock[i]);

        log_str("Shutting down server connection [%d]", srv->csock.chan.fd);
        tcp_sock_shutdown_(&srv->csock, 1);
}


void tcp_srv_foreach_client(tcp_srv_t *srv, tcp_foreach_func_t func, void *user_data)
{
	int i;

	for (i = 0; i < TCP_SRV_MAX_CLIENTS; i++) {
		tcp_sock_t *tcp_sock = &srv->dsock[i];
		if (tcp_sock->chan.fd >= 0) {
			if (!func(tcp_sock, user_data)) {
				break;
			}
		}
	}
}
