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
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef ENABLE_LWS
#include <libwebsockets.h>
#endif

#include "types.h"
#include "log.h"
#include "sys.h"
#include "ws.h"


static struct libwebsocket_context *ws_context = NULL;
static char *resource_path = "/home/giroudon/Domo/hakit/lws/test";


/*
 * Protocol handling: HTTP
 */

struct per_session_data__http {
	int fd;
	unsigned char buffer[4096];
};


static int ws_callback_poll(struct libwebsocket_context *context, struct pollfd *pollfd)
{
	log_debug(3, "ws_callback_poll: %d %02X", pollfd->fd, pollfd->revents);

	if (libwebsocket_service_fd(context, pollfd) < 0) {
		return 0;
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

		fprintf(stderr, "    %s = %s\n", (char *)c, buf);  //REVISIT
		n++;
	} while (c);
}


static const char *get_mimetype(const char *file)
{
	int n = strlen(file);

	if (n < 5)
		return NULL;

	if (!strcmp(&file[n - 4], ".ico"))
		return "image/x-icon";

	if (!strcmp(&file[n - 4], ".png"))
		return "image/png";

	if (!strcmp(&file[n - 5], ".html"))
		return "text/html";

	if (!strcmp(&file[n - 3], ".js"))
		return "text/javascript";

	if (!strcmp(&file[n - 4], ".css"))
		return "text/css";

	return NULL;
}


static int ws_callback_http_request(struct libwebsocket_context *context,
				    struct libwebsocket *wsi,
				    struct per_session_data__http *pss,
				    void *in, size_t len)
{
	char buf[256];
	char leaf_path[1024];
	char b64[64];
	struct timeval tv;
	int n;
	unsigned char *p;
	char *other_headers;
	struct stat stat_buf;
	const char *mimetype;
	unsigned char *end;

	log_debug(2, "ws_callback_http_request: %d bytes", (int) len);
	log_debug_data((unsigned char *) in, len);

	dump_handshake_info(wsi);

	if (len < 1) {
		libwebsockets_return_http_status(context, wsi, HTTP_STATUS_BAD_REQUEST, NULL);
		goto try_to_reuse;
	}

	/* This server has no concept of directory listing */
	if (strchr((const char *)in + 1, '/')) {
		libwebsockets_return_http_status(context, wsi, HTTP_STATUS_FORBIDDEN, NULL);
		goto try_to_reuse;
	}

	/* If a legal POST URL, let it continue and accept data */
	if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI)) {
		return 0;
	}

	/* check for the "send a big file by hand" example case */
	if (strcmp((const char *)in, "/leaf.jpg") == 0) {
		if (strlen(resource_path) > (sizeof(leaf_path) - 10)) {
			return -1;
		}
		snprintf(leaf_path, sizeof(leaf_path), "%s/leaf.jpg", resource_path);

		/* well, let's demonstrate how to send the hard way */
		p = pss->buffer + LWS_SEND_BUFFER_PRE_PADDING;
		end = p + sizeof(pss->buffer) - LWS_SEND_BUFFER_PRE_PADDING;

		pss->fd = open(leaf_path, O_RDONLY);
		if (pss->fd < 0) {
			log_str("HTTP ERROR: Cannot open file '%s': %s", leaf_path, strerror(errno));
			return -1;
		}

		if (fstat(pss->fd, &stat_buf) < 0) {
			log_str("HTTP ERROR: Cannot stat file '%s': %s", leaf_path, strerror(errno));
			return -1;
		}

		/*
		 * we will send a big jpeg file, but it could be
		 * anything.  Set the Content-Type: appropriately
		 * so the browser knows what to do with it.
		 * 
		 * Notice we use the APIs to build the header, which
		 * will do the right thing for HTTP 1/1.1 and HTTP2
		 * depending on what connection it happens to be working
		 * on
		 */
		if (lws_add_http_header_status(context, wsi, 200, &p, end)) {
			return 1;
		}
		if (lws_add_http_header_by_token(context, wsi,
						 WSI_TOKEN_HTTP_SERVER,
						 (unsigned char *)"libwebsockets",
						 13, &p, end)) {
			return 1;
		}
		if (lws_add_http_header_by_token(context, wsi,
						 WSI_TOKEN_HTTP_CONTENT_TYPE,
						 (unsigned char *)"image/jpeg",
						 10, &p, end)) {
			return 1;
		}
		if (lws_add_http_header_content_length(context, wsi,
						       stat_buf.st_size, &p, end)) {
			return 1;
		}
		if (lws_finalize_http_header(context, wsi, &p, end)) {
			return 1;
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

		n = libwebsocket_write(wsi,
				       pss->buffer + LWS_SEND_BUFFER_PRE_PADDING,
				       p - (pss->buffer + LWS_SEND_BUFFER_PRE_PADDING),
				       LWS_WRITE_HTTP_HEADERS);

		if (n < 0) {
			close(pss->fd);
			return -1;
		}
		/*
		 * book us a LWS_CALLBACK_HTTP_WRITEABLE callback
		 */
		libwebsocket_callback_on_writable(context, wsi);
	}
	else {
		/* if not, send a file the easy way */
		strcpy(buf, resource_path);
		if (strcmp(in, "/")) {
			if (*((const char *)in) != '/') {
				strcat(buf, "/");
			}
			strncat(buf, in, sizeof(buf) - strlen(resource_path));
		} else { /* default file to serve */
			strcat(buf, "/test.html");
		}
		buf[sizeof(buf) - 1] = '\0';

		/* refuse to serve files we don't understand */
		mimetype = get_mimetype(buf);
		if (mimetype == NULL) {
			log_str("HTTP ERROR: Unknown mimetype for %s\n", buf);
			libwebsockets_return_http_status(context, wsi, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, NULL);
			return -1;
		}

		/* demostrates how to set a cookie on / */
		other_headers = NULL;
		n = 0;
		if (!strcmp((const char *)in, "/") &&
		    !lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_COOKIE)) {
			/* this isn't very unguessable but it'll do for us */
			gettimeofday(&tv, NULL);
			n = sprintf(b64, "test=LWS_%u_%u_COOKIE;Max-Age=360000",
				    (unsigned int)tv.tv_sec,
				    (unsigned int)tv.tv_usec);

			p = (unsigned char *)leaf_path;

			if (lws_add_http_header_by_name(context, wsi, 
							(unsigned char *)"set-cookie:", 
							(unsigned char *)b64, n, &p,
							(unsigned char *)leaf_path + sizeof(leaf_path)))
				return 1;
			n = (char *)p - leaf_path;
			other_headers = leaf_path;
		}

		n = libwebsockets_serve_http_file(context, wsi, buf, mimetype, other_headers, n);
		if (n < 0) {
			return -1;
		}
		if (n > 0) {
			goto try_to_reuse;
		}

		/*
		 * notice that the sending of the file completes asynchronously,
		 * we'll get a LWS_CALLBACK_HTTP_FILE_COMPLETION callback when
		 * it's done
		 */
	}

	return 0;

try_to_reuse:
	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}

	return 0;
}


static int ws_callback_http_body(struct libwebsocket_context *context,
				 struct libwebsocket *wsi,
				 void *in, size_t len)
{
	log_debug(2, "ws_callback_http_body: %d bytes", (int) len);
	log_debug_data((unsigned char *) in, len);

	return 0;
}


static int ws_callback_http_body_completion(struct libwebsocket_context *context,
					    struct libwebsocket *wsi)
{
	log_debug(2, "ws_callback_http_body_completion");

	libwebsockets_return_http_status(context, wsi, HTTP_STATUS_OK, NULL);
	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;
}


static int ws_callback_http_file_completion(struct libwebsocket_context *context,
					    struct libwebsocket *wsi)
{
	log_debug(2, "ws_callback_http_file_completion");

	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;
}


static int ws_callback_http_writeable(struct libwebsocket_context *context,
				      struct libwebsocket *wsi,
				      struct per_session_data__http *pss)
{
	int n, m;

	log_debug(2, "ws_callback_http_writeable");

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

		n = read(pss->fd, pss->buffer + LWS_SEND_BUFFER_PRE_PADDING, n);

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
			if (lseek(pss->fd, m - n, SEEK_CUR) < 0) {
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

	close(pss->fd);

	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;

bail:
	close(pss->fd);
	return -1;
}


static int ws_callback_http(struct libwebsocket_context *context,
			    struct libwebsocket *wsi,
			    enum libwebsocket_callback_reasons reason, void *user,
			    void *in, size_t len)
{
	struct per_session_data__http *pss = (struct per_session_data__http *) user;
	struct libwebsocket_pollargs *pa = (struct libwebsocket_pollargs *) in;
	int ret = 0;

	switch (reason) {
	case LWS_CALLBACK_HTTP:
		log_debug(3, "ws_callback_http LWS_CALLBACK_HTTP");
		ret = ws_callback_http_request(context, wsi, pss, in, len);
		break;
	case LWS_CALLBACK_HTTP_BODY:
		log_debug(3, "ws_callback_http LWS_CALLBACK_HTTP_BODY");
		ret = ws_callback_http_body(context, wsi, in, len);
		break;
	case LWS_CALLBACK_HTTP_BODY_COMPLETION:
		log_debug(3, "ws_callback_http LWS_CALLBACK_HTTP_BODY_COMPLETION");
		ret = ws_callback_http_body_completion(context, wsi);
		break;
	case LWS_CALLBACK_HTTP_FILE_COMPLETION:
		log_debug(3, "ws_callback_http LWS_CALLBACK_HTTP_FILE_COMPLETION");
		ret = ws_callback_http_file_completion(context, wsi);
		break;
	case LWS_CALLBACK_HTTP_WRITEABLE:
		log_debug(3, "ws_callback_http LWS_CALLBACK_HTTP_WRITEABLE");
		ret = ws_callback_http_writeable(context, wsi, pss);
		break;
	case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
		log_debug(3, "ws_callback_http LWS_CALLBACK_FILTER_NETWORK_CONNECTION");
		/* if we returned non-zero from here, we kill the connection */
		break;
	case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
		log_debug(3, "ws_callback_http LWS_CALLBACK_FILTER_HTTP_CONNECTION");
		/* if we returned non-zero from here, we kill the connection */
		break;
	case LWS_CALLBACK_LOCK_POLL:
		log_debug(3, "ws_callback_http LWS_CALLBACK_LOCK_POLL");
		/*
		 * lock mutex to protect pollfd state
		 * called before any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_UNLOCK_POLL:
		log_debug(3, "ws_callback_http LWS_CALLBACK_UNLOCK_POLL");
		/*
		 * unlock mutex to protect pollfd state when
		 * called after any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_ADD_POLL_FD:
		log_debug(3, "ws_callback_http LWS_CALLBACK_ADD_POLL_FD");
		sys_io_poll(pa->fd, pa->events, (sys_poll_func_t) ws_callback_poll, context);
		break;
	case LWS_CALLBACK_DEL_POLL_FD:
		log_debug(3, "ws_callback_http LWS_CALLBACK_DEL_POLL_FD");
		sys_remove_fd(pa->fd);
		break;
	case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
		log_debug(3, "ws_callback_http LWS_CALLBACK_CHANGE_MODE_POLL_FD");
		sys_io_poll(pa->fd, pa->events, (sys_poll_func_t) ws_callback_poll, context);
		break;
	default:
		log_debug(3, "ws_callback_http reason=%d", reason);
		break;
	}

	return ret;
}


/*
 * Protocol handling: HAKit events
 */

struct per_session_data__events {
	int number;
};


static int ws_callback_events(struct libwebsocket_context *context,
			    struct libwebsocket *wsi,
			    enum libwebsocket_callback_reasons reason, void *user,
			    void *in, size_t len)
{
	int n, m;
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + 512 + LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
	struct per_session_data__events *pss = (struct per_session_data__events *) user;

	log_debug(2, "ws_callback_events: reason=%d", reason);

	switch (reason) {
	case LWS_CALLBACK_ESTABLISHED:
		pss->number = 0;
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE:
		n = sprintf((char *)p, "%d", pss->number++);
		m = libwebsocket_write(wsi, p, n, LWS_WRITE_TEXT);
		if (m < n) {
			log_str("HTTP ERROR: %d writing to di socket\n", n);
			return -1;
		}
		break;

	case LWS_CALLBACK_RECEIVE:
//		fprintf(stderr, "rx %d\n", (int)len);
		if (len < 6) {
			break;
		}
		if (strcmp((const char *)in, "reset\n") == 0) {
			pss->number = 0;
		}
		break;

	/*
	 * this just demonstrates how to use the protocol filter. If you won't
	 * study and reject connections based on header content, you don't need
	 * to handle this callback
	 */
	case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
		dump_handshake_info(wsi);
		/* you could return non-zero here and kill the connection */
		break;

	default:
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
		.callback = ws_callback_http,
		.per_session_data_size = sizeof(struct per_session_data__http),
		.rx_buffer_size = 0,
	},
	{
		.name = "hakit-events",
		.callback = ws_callback_events,
		.per_session_data_size = sizeof(struct per_session_data__events),
		.rx_buffer_size = 10,
	},
	{ NULL, NULL, 0, 0 } /* terminator */
};


int ws_init(int port)
{
	struct lws_context_creation_info info;

	lwsl_notice("HAKit " xstr(HAKIT_VERSION));

	memset(&info, 0, sizeof info);
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

	return 0;
}


void ws_done(void)
{
	/* Destroy libwebsockets context */
	if (ws_context != NULL) {
		libwebsocket_context_destroy(ws_context);
		ws_context = NULL;
	}
}
