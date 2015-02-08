#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>

#include "types.h"
#include "log.h"
#include "options.h"
#include "buf.h"
#include "tcpio.h"
#include "http.h"
#include "http_server.h"


/*==================================================*/
/* Client connection context                        */
/*==================================================*/

typedef enum {
	ST_IDLE=0,
	ST_HEADER,
	ST_DATA,
	ST_N
} ctx_state_t;

typedef struct {
	http_server_t *server;
	tcp_sock_t *client;
	ctx_state_t state;
	buf_t hbuf;
	int content_length;
} ctx_t;


static ctx_t *ctx_new(http_server_t *server, tcp_sock_t *client)
{
	ctx_t *ctx = malloc(sizeof(ctx_t));
	ctx->server = server;
	ctx->client = client;
	ctx->state = ST_IDLE;
	buf_init(&ctx->hbuf);
	ctx->content_length = 0;
	return ctx;
}


static int ctx_del(ctx_t *ctx)
{
	tcp_sock_set_data(ctx->client, NULL);
	tcp_sock_shutdown(ctx->client);

	buf_cleanup(&ctx->hbuf);
	memset(ctx, 0, sizeof(ctx_t));

	free(ctx);

	return 0;
}


static const char *ctx_state_str(ctx_t *ctx)
{
	static const char *state_str[] = {
		"IDLE", "HEADER", "DATA",
	};

	if (ctx->state < ST_N) {
		return state_str[ctx->state];
	}

	return "?";
}


/*====================*/
/* HTTP helpers       */
/*====================*/

static inline int end_of_line(char c)
{
	return (c == '\0') || (c == '\r') || (c == '\n');
}


static inline int http_ok(unsigned char *str)
{
	static const char *ok = "HTTP/1.1 200 OK\r\n";
	return (strncmp((char *) str, ok, strlen(ok)) == 0);
}


static void http_server_recv_data(ctx_t *ctx, unsigned char *rbuf, int rsize)
{
	log_debug(2, "http_server_recv_data %d", rsize);
	ctx->content_length -= rsize;
}


static void http_send_error(ctx_t *ctx, char *str)
{
	int len = strlen(str);
	char content[2*len+256];
	int clen = snprintf(content, sizeof(content), "<html><head>%s</head><body>%s</body></html>\n", str, str);

	char header[len+256];
	int hlen = snprintf(header, sizeof(header), "HTTP/1.1 %s\r\nContent-Type: text/html\r\nContent-Length: %d\r\n\r\n", str, clen);

	log_debug(2, "TO-CLIENT [%d] size=%d", ctx->client->chan.fd, hlen);
	log_debug_data((unsigned char *) header, hlen);
	tcp_sock_write(ctx->client, (char *) header, hlen);
	tcp_sock_write(ctx->client, (char *) content, clen);
}


typedef struct {
	char *suffix;
	char *type;
} mime_type_t;

static const mime_type_t mime_types[] = {
	{".html", "text/html"},
	{".htm",  "text/html"},
	{".png",  "image/png"},
	{".jpg",  "image/jpeg"},
};

static char *http_mime_type(char *fname)
{
	char *suffix = &fname[strlen(fname)];
	while ((suffix > fname) && (*suffix != '.')) {
		suffix--;
	}

	int i;
	for (i = 0; i < ARRAY_SIZE(mime_types); i++) {
		const mime_type_t *mime = &mime_types[i];
		if (strcmp(suffix, mime->suffix) == 0) {
			return mime->type;
		}
	}

	return "text/plain";
}


int http_send_file(tcp_sock_t *client, char *fname)
{
	FILE *f;

	log_debug(1, "FILE: %s", fname);

	/* Open file */
	f = fopen(fname, "r");
	if (f == NULL) {
		return 0;
	}

	/* Determine file mime type */
	char *ctype = http_mime_type(fname);

	/* Determine file size */
	fseek(f, 0, SEEK_END);
	int clen = ftell(f);
	fseek(f, 0, SEEK_SET);

	char header[256];
	int hlen = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", ctype, clen);

	log_debug(2, "TO-CLIENT [%d] size=%d", client->chan.fd, hlen);
	log_debug_data((unsigned char *) header, hlen);
	tcp_sock_write(client, (char *) header, hlen);

	while (!feof(f)) {
		char buf[BUFSIZ];
		int size = fread(buf, 1, sizeof(buf), f);
		if (size < 0) {
			log_str("Error reading file '%s': %s", fname, strerror(errno));
			break;
		}
		else if (size > 0) {
			tcp_sock_write(client, (char *) buf, size);
			log_debug(2, "TO-CLIENT [%d] size=%d", client->chan.fd, size);
		}
	}

	fclose(f);

	return 1;
}


void http_send_buf(tcp_sock_t *client, char *ctype, buf_t *cbuf)
{
	char header[256];
	int hlen = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n", ctype, cbuf->len);

	log_debug(2, "TO-CLIENT [%d] size=%d", client->chan.fd, hlen);
	log_debug_data((unsigned char *) header, hlen);
	tcp_sock_write(client, (char *) header, hlen);

	tcp_sock_write(client, (char *) cbuf->base, cbuf->len);
}


static void http_server_process_command(ctx_t *ctx)
{
	char *str = (char *) ctx->hbuf.base;
	int len = 0;
	char c_end;
	int i;

	while ((len <= ctx->hbuf.len) && (!end_of_line(str[len]))) {
		len++;
	}

	c_end = str[len];
	str[len] = '\0';

	log_debug(1, "COMMAND: %s", str);

	/* Extract GET location */
	char *location = NULL;
	if (strncmp(str, "GET ", 4) == 0) {
		char *s1 = str + 4;
		while ((*s1 != '\0') && (*s1 <= ' ')) {
			s1++;
		}

		char *s2 = s1;
		while (*s2 > ' ') {
			s2++;
		}

		char c2 = *s2;
		*s2 = '\0';
		location = strdup(s1);
		*s2 = c2;
	}

	/* Clear header buffer */
	buf_cleanup(&ctx->hbuf);

	/* Construct response from requested location */
	int found = 0;
	if (location != NULL) {
		log_str("%s", location);

		/* Process aliases */
		for (i = 0; i < ctx->server->naliases; i++) {
			http_server_alias_t *alias = &ctx->server->aliases[i];
			if (strncmp(location, alias->location, alias->len) == 0) {
				found = 1;
				alias->handler(ctx->client, location);
				break;
			}
		}

		/* Process files if no alias found */
		if (!found) {
			if (ctx->server->document_root != NULL) {
				char path[strlen(ctx->server->document_root)+strlen(location)+2];
				snprintf(path, sizeof(path), "%s%s", ctx->server->document_root, location);
				found = http_send_file(ctx->client, path);
			}
		}

		free(location);
	}

	if (!found) {
		http_send_error(ctx,"404 NOT FOUND");
	}

	str[len] = c_end;

	ctx->state = ST_IDLE;
}


static void http_server_recv(ctx_t *ctx, unsigned char *rbuf, int rsize)
{
	int roffset = 0;

	if (opt_debug >= 1) {
		log_printf("DATA [%d] state=%s\n", rsize, ctx_state_str(ctx));
	}
	log_debug_data(rbuf, rsize);

	switch (ctx->state) {
	case ST_IDLE:
		ctx->state = ST_HEADER;
		buf_cleanup(&ctx->hbuf);
		ctx->content_length = 0;
	case ST_HEADER:
		roffset = http_recv_header(&ctx->hbuf, rbuf, rsize, &ctx->content_length);
		if (roffset > 0) {
			if (ctx->content_length > 0) {
				ctx->state = ST_DATA;
				if (roffset < rsize) {
					http_server_recv_data(ctx, rbuf+roffset, rsize-roffset);
				}
			}

			if (ctx->content_length <= 0) {
				http_server_process_command(ctx);
			}
		}
		break;
	case ST_DATA:
		http_server_recv_data(ctx, rbuf, rsize);
		if (ctx->content_length <= 0) {
			http_server_process_command(ctx);
		}
		break;
	default:
		break;
	}
}


static void http_server_callback(tcp_sock_t *tcp_sock, tcp_io_t io, char *rbuf, int rsize)
{
	ctx_t *ctx;

	if (opt_debug >= 1) {
		log_tstamp();
		log_printf("HTTP [%d] ", tcp_sock->chan.fd);
	}

	switch (io) {
	case TCP_IO_CONNECT:
		if (opt_debug >= 1) {
			log_printf("CONNECT = %s\n", rbuf);
		}
		http_server_t *http = tcp_sock_get_data(tcp_sock);
		tcp_sock_set_data(tcp_sock, ctx_new(http, tcp_sock));
		break;
	case TCP_IO_DATA:
		ctx = tcp_sock_get_data(tcp_sock);
		http_server_recv(ctx, (unsigned char *) rbuf, rsize);
		break;
	case TCP_IO_HUP:
		if (opt_debug >= 1) {
			log_printf("HUP\n");
		}
		ctx = tcp_sock_get_data(tcp_sock);
		ctx_del(ctx);
		break;
	default:
		if (opt_debug >= 1) {
			log_printf("(UNKNOWN)\n");
		}
		break;
	}
}


http_server_t *http_server_new(int port, char *document_root)
{
	http_server_t *http = malloc(sizeof(http_server_t));
	memset(http, 0, sizeof(http_server_t));

	tcp_srv_init(&http->server, port, http_server_callback, http);

	if (document_root != NULL) {
		http->document_root = strdup(document_root);
	}

	return http;
}


void http_server_alias(http_server_t *http, char *location, http_server_handler_t handler)
{
	http->naliases++;
	http->aliases = realloc(http->aliases, http->naliases * sizeof(http_server_alias_t));
	http_server_alias_t *alias = &http->aliases[http->naliases-1];
	alias->location = strdup(location);
	alias->len = strlen(location);
	alias->handler = handler;
}
