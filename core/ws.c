/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2015 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>

#include <libwebsockets.h>

#include "types.h"
#include "log.h"
#include "sys.h"
#include "ws.h"
#include "hakit_version.h"

#define SERVER_NAME "HAKit"

static struct libwebsocket_context *ws_context = NULL;
static char *document_root = NULL;
static int document_root_len = 0;


/*
 * Mime types
 */

typedef struct {
	const char *suffix;
	const char *type;
} ws_mime_t;

static const ws_mime_t ws_mimes[] = {
	{"ico",  "image/x-icon"},
	{"png",  "image/png"},
	{"jpg",  "image/jpeg"},
	{"html", "text/html"},
	{"js",   "text/javascript"},
	{"css",  "text/css"},
	{NULL, NULL}
};


static const char *get_mimetype(const char *file)
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


/*
 * Protocol handling: HTTP
 */

struct per_session_data__http {
	FILE *f;
	unsigned char buffer[4096];
};


static int ws_callback_poll(struct libwebsocket_context *context, struct pollfd *pollfd)
{
	int ret;

	log_debug(3, "ws_callback_poll: %d %02X", pollfd->fd, pollfd->revents);

	ret = libwebsocket_service_fd(context, pollfd);
	if (ret < 0) {
		log_debug(3, "  => %d", ret);
	}
	return 1;
}


static void dump_handshake_info(struct libwebsocket *wsi)
{
	int n = 0;
	char buf[256];
	const unsigned char *c;

	do {
		c = lws_token_to_string(n);
		if (!c) {
			n++;
			continue;
		}

		if (!lws_hdr_total_length(wsi, n)) {
			n++;
			continue;
		}

		lws_hdr_copy(wsi, buf, sizeof buf, n);

//		fprintf(stderr, "    %s = %s\n", (char *)c, buf);  //REVISIT
		n++;
	} while (c);
}


static int ws_http_request(struct libwebsocket_context *context,
			   struct libwebsocket *wsi,
			   struct per_session_data__http *pss,
			   char *uri, size_t len)
{
	char *file_path = NULL;
	int file_path_size;
	int file_path_len;
	int file_size;
	const char *mimetype;
	unsigned char *p;
	unsigned char *end;
	int ret = 1;

	log_debug(2, "ws_http_request: %d bytes", (int) len);
	log_debug_data((unsigned char *) uri, len);

	dump_handshake_info(wsi);

	if (len < 1) {
		libwebsockets_return_http_status(context, wsi, HTTP_STATUS_BAD_REQUEST, NULL);
		goto try_to_reuse;
	}

	/* This server has no concept of directory listing */
	if (strchr(uri+1, '/') != NULL) {
		libwebsockets_return_http_status(context, wsi, HTTP_STATUS_FORBIDDEN, NULL);
		goto try_to_reuse;
	}

	/* If a legal POST URL, let it continue and accept data */
	if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI)) {
		return 0;
	}

	//TODO: try to match URL aliases here

	/* Construct full file name */
	file_path_size = document_root_len + len + 20;
	file_path = malloc(file_path_size);
	file_path_len = snprintf(file_path, file_path_size, "%s/%s", document_root, uri);

	if (file_path[file_path_len-1] == '/') {
		strcpy(file_path+file_path_len, "index.html");
	}

	/* Open file */
	pss->f = fopen(file_path, "r");
	if (pss->f == NULL) {
		log_str("HTTP ERROR: Cannot open file '%s': %s", file_path, strerror(errno));
		goto failed;
	}

	/* Get and check mime type */
	mimetype = get_mimetype(file_path);
	if (mimetype == NULL) {
		log_str("HTTP ERROR: Unknown mimetype for '%s'", file_path);
		libwebsockets_return_http_status(context, wsi, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, NULL);
		goto failed;
	}

	/* Get file size */
	fseek(pss->f, 0, SEEK_END);
	file_size = ftell(pss->f);
	fseek(pss->f, 0, SEEK_SET);

	/*
	 * Construct HTTP header.
	 * Notice we use the APIs to build the header, which
	 * will do the right thing for HTTP 1/1.1 and HTTP2
	 * depending on what connection it happens to be working
	 * on
	 */
	p = pss->buffer + LWS_SEND_BUFFER_PRE_PADDING;
	end = p + sizeof(pss->buffer) - LWS_SEND_BUFFER_PRE_PADDING;

	if (lws_add_http_header_status(context, wsi, 200, &p, end)) {
		goto done;
	}
	if (lws_add_http_header_by_token(context, wsi,
					 WSI_TOKEN_HTTP_SERVER,
					 (unsigned char *) SERVER_NAME, strlen(SERVER_NAME),
					 &p, end)) {
		goto done;
	}
	if (lws_add_http_header_by_token(context, wsi,
					 WSI_TOKEN_HTTP_CONTENT_TYPE,
					 (unsigned char *) mimetype, strlen(mimetype),
					 &p, end)) {
		goto done;
	}
	if (lws_add_http_header_content_length(context, wsi, file_size, &p, end)) {
		goto done;
	}
	if (lws_finalize_http_header(context, wsi, &p, end)) {
		goto done;
	}

	/*
	 * send the http headers...
	 * this won't block since it's the first payload sent
	 * on the connection since it was established
	 * (too small for partial)
	 * 
	 * Notice they are sent using LWS_WRITE_HTTP_HEADERS
	 * which also means you can't send body too in one step,
	 * this is mandated by changes in HTTP2
	 */
	if (libwebsocket_write(wsi,
			       pss->buffer + LWS_SEND_BUFFER_PRE_PADDING,
			       p - (pss->buffer + LWS_SEND_BUFFER_PRE_PADDING),
			       LWS_WRITE_HTTP_HEADERS) < 0) {
		goto failed;
	}

	/*
	 * book us a LWS_CALLBACK_HTTP_WRITEABLE callback
	 */
	libwebsocket_callback_on_writable(context, wsi);

	ret = 0;
	goto done;

try_to_reuse:
	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}

	return 0;

failed:
	ret = -1;

	if (pss->f != NULL) {
		fclose(pss->f);
		pss->f = NULL;
	}

done:
	if (file_path != NULL) {
		free(file_path);
		file_path = NULL;
	}

	return ret;
}


static int ws_http_body(struct libwebsocket_context *context,
			struct libwebsocket *wsi,
			void *in, size_t len)
{
	log_debug(2, "ws_http_body: %d bytes", (int) len);
	log_debug_data((unsigned char *) in, len);

	return 0;
}


static int ws_http_body_completion(struct libwebsocket_context *context,
					    struct libwebsocket *wsi)
{
	log_debug(2, "ws_http_body_completion");

	libwebsockets_return_http_status(context, wsi, HTTP_STATUS_OK, NULL);
	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;
}


static int ws_http_file_completion(struct libwebsocket_context *context,
					    struct libwebsocket *wsi)
{
	log_debug(2, "ws_http_file_completion");

	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;
}


static int ws_http_writeable(struct libwebsocket_context *context,
				      struct libwebsocket *wsi,
				      struct per_session_data__http *pss)
{
	int n, m;

	log_debug(2, "ws_http_writeable");

	/* We can send more of whatever it is we were sending */
	do {
		/* we'd like the send this much */
		n = sizeof(pss->buffer) - LWS_SEND_BUFFER_PRE_PADDING;
			
		/* but if the peer told us he wants less, we can adapt */
		m = lws_get_peer_write_allowance(wsi);

		/* -1 means not using a protocol that has this info */
		if (m == 0) {
			/* right now, peer can't handle anything */
			goto later;
		}

		if ((m != -1) && (m < n)) {
			/* he couldn't handle that much */
			n = m;
		}

		n = fread(pss->buffer + LWS_SEND_BUFFER_PRE_PADDING, 1, n, pss->f);

		/* problem reading, close conn */
		if (n < 0) {
			log_str("HTTP ERROR: Cannot read file: %s", strerror(errno));
			goto bail;
		}
		/* sent it all, close conn */
		if (n == 0) {
			goto flush_bail;
		}

		/*
		 * To support HTTP2, must take care about preamble space
		 * 
		 * identification of when we send the last payload frame
		 * is handled by the library itself if you sent a
		 * content-length header
		 */
		m = libwebsocket_write(wsi,
				       pss->buffer + LWS_SEND_BUFFER_PRE_PADDING,
				       n, LWS_WRITE_HTTP);
		if (m < 0) {
			/* write failed, close conn */
			goto bail;
		}

		/*
		 * http2 won't do this
		 */
		if (m != n) {
			/* partial write, adjust */
			if (fseek(pss->f, m - n, SEEK_CUR) < 0) {
				goto bail;
			}
		}

		if (m) {
			/* while still active, extend timeout */
			libwebsocket_set_timeout(wsi, PENDING_TIMEOUT_HTTP_CONTENT, 5);
		}

		/* if we have indigestion, let him clear it before eating more */
		if (lws_partial_buffered(wsi)) {
				break;
		}

	} while (!lws_send_pipe_choked(wsi));

later:
	libwebsocket_callback_on_writable(context, wsi);
	return 0;

flush_bail:
	/* true if still partial pending */
	if (lws_partial_buffered(wsi)) {
		libwebsocket_callback_on_writable(context, wsi);
		return 0;
	}

	fclose(pss->f);
	pss->f = NULL;

	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;

bail:
	fclose(pss->f);
	pss->f = NULL;

	return -1;
}


static int ws_http_callback(struct libwebsocket_context *context,
			    struct libwebsocket *wsi,
			    enum libwebsocket_callback_reasons reason, void *user,
			    void *in, size_t len)
{
	struct per_session_data__http *pss = (struct per_session_data__http *) user;
	struct libwebsocket_pollargs *pa = (struct libwebsocket_pollargs *) in;
	int ret = 0;

	switch (reason) {
	case LWS_CALLBACK_HTTP:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP");
		ret = ws_http_request(context, wsi, pss, in, len);
		break;
	case LWS_CALLBACK_HTTP_BODY:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_BODY");
		ret = ws_http_body(context, wsi, in, len);
		break;
	case LWS_CALLBACK_HTTP_BODY_COMPLETION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_BODY_COMPLETION");
		ret = ws_http_body_completion(context, wsi);
		break;
	case LWS_CALLBACK_HTTP_FILE_COMPLETION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_FILE_COMPLETION");
		ret = ws_http_file_completion(context, wsi);
		break;
	case LWS_CALLBACK_HTTP_WRITEABLE:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_WRITEABLE");
		ret = ws_http_writeable(context, wsi, pss);
		break;
	case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_FILTER_NETWORK_CONNECTION");
		/* if we returned non-zero from here, we kill the connection */
		break;
	case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_FILTER_HTTP_CONNECTION");
		/* if we returned non-zero from here, we kill the connection */
		break;
	case LWS_CALLBACK_LOCK_POLL:
		log_debug(3, "ws_http_callback LWS_CALLBACK_LOCK_POLL %d", pa->fd);
		/*
		 * lock mutex to protect pollfd state
		 * called before any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_UNLOCK_POLL:
		log_debug(3, "ws_http_callback LWS_CALLBACK_UNLOCK_POLL %d", pa->fd);
		/*
		 * unlock mutex to protect pollfd state when
		 * called after any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_ADD_POLL_FD:
		log_debug(3, "ws_http_callback LWS_CALLBACK_ADD_POLL_FD %d %02X", pa->fd, pa->events);
		sys_io_poll(pa->fd, pa->events, (sys_poll_func_t) ws_callback_poll, context);
		break;
	case LWS_CALLBACK_DEL_POLL_FD:
		log_debug(3, "ws_http_callback LWS_CALLBACK_DEL_POLL_FD %d", pa->fd);
		sys_remove_fd(pa->fd);
		break;
	case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
		log_debug(3, "ws_http_callback LWS_CALLBACK_CHANGE_MODE_POLL_FD %d %02X", pa->fd, pa->events);
		sys_io_poll(pa->fd, pa->events, (sys_poll_func_t) ws_callback_poll, context);
		break;
	default:
		log_debug(3, "ws_http_callback reason=%d", reason);
		break;
	}

	return ret;
}


/*
 * Protocol handling: HAKit events
 */

struct per_session_data__events {
	int number;
	int update;
	sys_tag_t tag;
};


static int ws_events_writeable(struct per_session_data__events *pss);


static int ws_events_callback(struct libwebsocket_context *context,
			      struct libwebsocket *wsi,
			      enum libwebsocket_callback_reasons reason, void *user,
			      void *in, size_t len)
{
	int n, m;
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + 512 + LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
	struct per_session_data__events *pss = (struct per_session_data__events *) user;


	switch (reason) {
	case LWS_CALLBACK_ESTABLISHED:
		log_debug(2, "ws_events_callback LWS_CALLBACK_ESTABLISHED %p", pss);
		pss->number = 0;
		pss->update = 0;
		pss->tag = sys_timeout(1000, (sys_func_t) ws_events_writeable, pss);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		if (pss->update) {
			log_debug(2, "ws_events_callback LWS_CALLBACK_SERVER_WRITEABLE %p", pss);
			n = sprintf((char *)p, "%d", pss->number);
			m = libwebsocket_write(wsi, p, n, LWS_WRITE_TEXT);
			if (m < n) {
				log_str("HTTP ERROR: %d writing to di socket", n);
				return -1;
			}
			pss->update = 0;
		}
		break;

	case LWS_CALLBACK_RECEIVE:
		log_debug(2, "ws_events_callback LWS_CALLBACK_RECEIVE %p", pss);
		log_debug_data(in, len);

		if (len >= 6) {
			if (strcmp((const char *)in, "reset\n") == 0) {
				pss->number = 0;
			}
		}
		break;

	case LWS_CALLBACK_CLOSED:
		log_debug(2, "ws_events_callback LWS_CALLBACK_CLOSED %p", pss);
		sys_remove(pss->tag);
		pss->tag = 0;
		break;

	/*
	 * this just demonstrates how to use the protocol filter. If you won't
	 * study and reject connections based on header content, you don't need
	 * to handle this callback
	 */
	case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
		log_debug(2, "ws_events_callback LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION");
		dump_handshake_info(wsi);
		/* you could return non-zero here and kill the connection */
		break;

	default:
		log_debug(2, "ws_events_callback: reason=%d", reason);
		break;
	}

	return 0;
}



/*
 * Table of available protocols
 */

static struct libwebsocket_protocols ws_protocols[] = {
	/* first protocol must always be HTTP handler */

	{
		.name = "http-only",
		.callback = ws_http_callback,
		.per_session_data_size = sizeof(struct per_session_data__http),
		.rx_buffer_size = 0,
	},
	{
		.name = "dumb-increment-protocol",
		.callback = ws_events_callback,
		.per_session_data_size = sizeof(struct per_session_data__events),
		.rx_buffer_size = 10,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};


static int ws_events_writeable(struct per_session_data__events *pss)
{
	log_debug(2, "--- ws_events_writeable number=%d", pss->number);

	pss->number++;
	pss->update = 1;
	libwebsocket_callback_on_writable_all_protocol(&ws_protocols[1]);

	return 1;
}


/*
 * HTTP/WS init
 */

int ws_init(int port, char *dir)
{
	struct lws_context_creation_info info;

	lwsl_notice(SERVER_NAME " " HAKIT_VERSION);

	memset(&info, 0, sizeof(info));
	info.port = port;
	info.protocols = ws_protocols;
	//info.extensions = libwebsocket_get_internal_extensions();

	/* TODO: Setup SSL info */
//	if (use_ssl) {
//		info.ssl_cert_filepath = cert_path;
//		info.ssl_private_key_filepath = key_path;
//	}

	info.gid = -1;
	info.uid = -1;

	/* Create libwebsockets context */
	ws_context = libwebsocket_create_context(&info);
	if (ws_context == NULL) {
		log_str("ERROR: libwebsocket init failed");
		return -1;
	}

	/* Set document root directory name */
	document_root = strdup(dir);
	document_root_len = strlen(document_root);

	return 0;
}


void ws_done(void)
{
	/* Free document root directory name */
	if (document_root != NULL) {
		free(document_root);
		document_root = NULL;
	}
	document_root_len = 0;

	/* Destroy libwebsockets context */
	if (ws_context != NULL) {
		libwebsocket_context_destroy(ws_context);
		ws_context = NULL;
	}
}
