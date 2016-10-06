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
#include <dirent.h>

#include "hakit_version.h"
#include "options.h"
#include "log.h"
#include "sys.h"
#include "tab.h"
#include "mime.h"
#include "ws.h"
#include "ws_utils.h"
#include "ws_events.h"

#define SERVER_NAME "HAKit"


/*
 * Protocol handling: HTTP
 */

struct per_session_data__http {
	FILE *f;
	buf_t rsp;
	int offset;
	unsigned char tx_buffer[4096];
};


static int ws_callback_poll(struct lws_context *context, struct pollfd *pollfd)
{
	int ret;

	log_debug(3, "ws_callback_poll: %d %02X", pollfd->fd, pollfd->revents);

	ret = lws_service_fd(context, pollfd);
	if (ret < 0) {
		log_debug(3, "  => %d", ret);
	}
	return 1;
}


static char *search_file(ws_t *ws, char *uri, size_t uri_len)
{
	int i;

	for (i = 0; i < ws->document_roots.nmemb; i++) {
		char *dir = HK_TAB_VALUE(ws->document_roots, char *, i);
		int file_path_len = strlen(dir);
		int file_path_size = file_path_len + uri_len + 20;
		char *file_path = malloc(file_path_size);

		strcpy(file_path, dir);

		if (uri[0] != '/') {
			file_path[file_path_len++] = '/';
		}

		strcpy(file_path+file_path_len, uri);
		file_path_len += uri_len;

		/* Check if URI targets a directory */
		DIR *d = opendir(file_path);
		if (d != NULL) {
			closedir(d);

			/* Try to access 'index.html' in this directory */
			if (file_path[file_path_len-1] != '/') {
				file_path[file_path_len++] = '/';
			}
			strcpy(file_path+file_path_len, "index.html");
		}

		FILE *f = fopen(file_path, "r");
		if (f != NULL) {
			fclose(f);
			return file_path;
		}

		free(file_path);
		file_path = NULL;
	}

	return NULL;
}


static int ws_http_request(ws_t *ws,
			   struct lws *wsi,
			   struct per_session_data__http *pss,
			   char *uri, size_t len)
{
	char *file_path = NULL;
	int content_length = 0;
	const char *mimetype = NULL;
	int ret = 1;
	unsigned char *p;
	unsigned char *end;
	int i;

	log_debug(2, "ws_http_request: %d bytes", (int) len);
	log_debug_data((unsigned char *) uri, len);

	ws_dump_handshake_info(wsi);

	if (len < 1) {
		lws_return_http_status(wsi, HTTP_STATUS_BAD_REQUEST, NULL);
		goto try_to_reuse;
	}

	/* If a legal POST URL, let it continue and accept data */
	if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI)) {
		log_debug(2, "=> POST");
		return 0;
	}

	/* Clear data source settings */
	pss->f = NULL;
	buf_init(&pss->rsp);
	pss->offset = 0;

	/* Try to match URL aliases */
	for (i = 0; i < ws->aliases.nmemb; i++) {
		ws_alias_t *alias = HK_TAB_PTR(ws->aliases, ws_alias_t, i);
		if ((alias->location != NULL) && (alias->handler != NULL)) {
			if (strncmp(alias->location, uri, alias->len) == 0) {
				alias->handler(alias->user_data, uri, &pss->rsp);
				break;
			}
		}
	}

	/* if no alias matched, read file */
	if (pss->rsp.len > 0) {
		content_length = pss->rsp.len;
	}
	else {
		/* Search file among root directory list */
		file_path = search_file(ws, uri, len);
		if (file_path == NULL) {
			log_str("HTTP ERROR: Cannot open file '%s': %s", file_path, strerror(errno));
			lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
			goto failed;
		}

		/* Get and check mime type */
		mimetype = get_mimetype(file_path);
		if (mimetype == NULL) {
			log_str("HTTP ERROR: Unknown mimetype for '%s'", file_path);
			lws_return_http_status(wsi, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, NULL);
			goto failed;
		}

		/* Open file */
		pss->f = fopen(file_path, "r");
		if (pss->f == NULL) {
			log_str("HTTP ERROR: Cannot open file '%s': %s", file_path, strerror(errno));
			lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
			goto failed;
		}

		/* Get file size */
		fseek(pss->f, 0, SEEK_END);
		content_length = ftell(pss->f);
		fseek(pss->f, 0, SEEK_SET);

		log_debug(2, "=> '%s' %s (%d bytes)", file_path, mimetype, content_length);
	}

	/*
	 * Construct HTTP header.
	 * Notice we use the APIs to build the header, which
	 * will do the right thing for HTTP 1/1.1 and HTTP2
	 * depending on what connection it happens to be working
	 * on
	 */
	p = pss->tx_buffer + LWS_SEND_BUFFER_PRE_PADDING;
	end = p + sizeof(pss->tx_buffer) - LWS_SEND_BUFFER_PRE_PADDING;

	if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end)) {
		goto done;
	}
	if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_SERVER,
					 (unsigned char *) SERVER_NAME, strlen(SERVER_NAME),
					 &p, end)) {
		goto done;
	}
	if (mimetype != NULL) {
		if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
						 (unsigned char *) mimetype, strlen(mimetype),
						 &p, end)) {
			goto done;
		}
	}
	if (content_length > 0) {
		if (lws_add_http_header_content_length(wsi, content_length, &p, end)) {
			goto done;
		}
	}
	if (lws_finalize_http_header(wsi, &p, end)) {
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
	if (lws_write(wsi,
		      pss->tx_buffer + LWS_SEND_BUFFER_PRE_PADDING,
		      p - (pss->tx_buffer + LWS_SEND_BUFFER_PRE_PADDING),
		      LWS_WRITE_HTTP_HEADERS) < 0) {
		goto failed;
	}

	/*
	 * book us a LWS_CALLBACK_HTTP_WRITEABLE callback
	 */
	lws_callback_on_writable(wsi);

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


static int ws_http_body(ws_t *ws,
			struct lws *wsi,
			void *in, size_t len)
{
	log_debug(2, "ws_http_body: %d bytes", (int) len);
	log_debug_data((unsigned char *) in, len);

	return 0;
}


static int ws_http_body_completion(ws_t *ws,
				   struct lws *wsi)
{
	log_debug(2, "ws_http_body_completion");

	lws_return_http_status(wsi, HTTP_STATUS_OK, NULL);
	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;
}


static int ws_http_file_completion(ws_t *ws,
				   struct lws *wsi)
{
	log_debug(2, "ws_http_file_completion");

	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;
}


static int ws_http_writeable(ws_t *ws,
			     struct lws *wsi,
			     struct per_session_data__http *pss)
{
	int n, m;

	log_debug(2, "ws_http_writeable");

	/* We can send more of whatever it is we were sending */
	do {
		/* we'd like the send this much */
		n = sizeof(pss->tx_buffer) - LWS_SEND_BUFFER_PRE_PADDING;
			
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

		if (pss->f != NULL) {
			n = fread(pss->tx_buffer + LWS_SEND_BUFFER_PRE_PADDING, 1, n, pss->f);
			if (n < 0) {
				log_str("HTTP ERROR: Cannot read file: %s", strerror(errno));
				goto bail;
			}
		}
		else {
			int len = pss->rsp.len - pss->offset;
			if (n > len) {
				n = len;
			}

			if (n > 0) {
				memcpy(pss->tx_buffer + LWS_SEND_BUFFER_PRE_PADDING, pss->rsp.base, n);
				pss->offset += n;
			}
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
		m = lws_write(wsi,
			      pss->tx_buffer + LWS_SEND_BUFFER_PRE_PADDING,
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
			lws_set_timeout(wsi, PENDING_TIMEOUT_HTTP_CONTENT, 5);
		}

		/* if we have indigestion, let him clear it before eating more */
		if (lws_partial_buffered(wsi)) {
				break;
		}

	} while (!lws_send_pipe_choked(wsi));

later:
	lws_callback_on_writable(wsi);
	return 0;

flush_bail:
	/* true if still partial pending */
	if (lws_partial_buffered(wsi)) {
		lws_callback_on_writable(wsi);
		return 0;
	}

	if (pss->f != NULL) {
		fclose(pss->f);
		pss->f = NULL;
	}

	buf_cleanup(&pss->rsp);

	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;

bail:
	if (pss->f != NULL) {
		fclose(pss->f);
		pss->f = NULL;
	}

	buf_cleanup(&pss->rsp);

	return -1;
}


static int ws_http_callback(struct lws *wsi,
			    enum lws_callback_reasons reason, void *user,
			    void *in, size_t len)
{
	struct lws_context *context = lws_get_context(wsi);
	ws_t *ws = lws_context_user(context);
	struct per_session_data__http *pss = (struct per_session_data__http *) user;
	struct lws_pollargs *pa = (struct lws_pollargs *) in;
	int ret = 0;

	switch (reason) {
	case LWS_CALLBACK_HTTP:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP");
		ret = ws_http_request(ws, wsi, pss, in, len);
		break;
	case LWS_CALLBACK_HTTP_BODY:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_BODY");
		ret = ws_http_body(ws, wsi, in, len);
		break;
	case LWS_CALLBACK_HTTP_BODY_COMPLETION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_BODY_COMPLETION");
		ret = ws_http_body_completion(ws, wsi);
		break;
	case LWS_CALLBACK_HTTP_FILE_COMPLETION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_FILE_COMPLETION");
		ret = ws_http_file_completion(ws, wsi);
		break;
	case LWS_CALLBACK_HTTP_WRITEABLE:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_WRITEABLE");
		ret = ws_http_writeable(ws, wsi, pss);
		break;
	case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_FILTER_NETWORK_CONNECTION");
		/* if we returned non-zero from here, we kill the connection */
		break;
	case LWS_CALLBACK_FILTER_HTTP_CONNECTION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_FILTER_HTTP_CONNECTION");
		/* if we returned non-zero from here, we kill the connection */
		break;
	case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED:
		log_debug(3, "ws_http_callback LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED");
		break;

	case LWS_CALLBACK_PROTOCOL_INIT:
		log_debug(3, "ws_http_callback LWS_CALLBACK_PROTOCOL_INIT");
		break;
	case LWS_CALLBACK_PROTOCOL_DESTROY:
		log_debug(3, "ws_http_callback LWS_CALLBACK_PROTOCOL_DESTROY");
		break;
	case LWS_CALLBACK_WSI_CREATE:
		log_debug(3, "ws_http_callback LWS_CALLBACK_WSI_CREATE");
		break;
	case LWS_CALLBACK_WSI_DESTROY:
		log_debug(3, "ws_http_callback LWS_CALLBACK_WSI_DESTROY");
		break;

	case LWS_CALLBACK_LOCK_POLL:
		//log_debug(3, "ws_http_callback LWS_CALLBACK_LOCK_POLL");
		/*
		 * lock mutex to protect pollfd state
		 * called before any other POLL related callback
		 */
		break;
	case LWS_CALLBACK_UNLOCK_POLL:
		//log_debug(3, "ws_http_callback LWS_CALLBACK_UNLOCK_POLL");
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
 * Table of available protocols
 */

static struct lws_protocols ws_protocols[] = {
	/* first protocol must always be HTTP handler */
	{
		.name = "http-only",
		.callback = ws_http_callback,
		.per_session_data_size = sizeof(struct per_session_data__http),
		.rx_buffer_size = 0,
	},
	{ }, /* Room for hakit-events-protocol */
	{ NULL, NULL, 0, 0 } /* terminator */
};


/*
 * HTTP/WebSocket server init
 */

static void ws_log(int level, const char *line)
{
	static const char *slevel[] = {
		"ERROR  ", "WARN   ", "NOTICE ", "INFO   ",
		"DEBUG  ", "PARSER ", "HEADER ", "EXT    ",
		"CLIENT ", "LATENCY",
	};
	int ilevel = 0;
	char *tag = "";

	for (ilevel = 0; ilevel < LLL_COUNT; ilevel++) {
		if (level & (1 << ilevel)) {
			break;
		}
	}

	if (ilevel < ARRAY_SIZE(slevel)) {
		tag = (char *) slevel[ilevel];
	}

	log_tstamp();
	log_printf("LWS %s : %s", tag, line);
}


ws_t *ws_new(int port, char *ssl_dir)
{
	ws_t *ws = NULL;
#ifdef WITH_SSL
	int ssl_dir_len = ssl_dir ? strlen(ssl_dir) : 0;
	char cert_path[ssl_dir_len+16];
	char key_path[ssl_dir_len+16];
#endif
	struct lws_context_creation_info info;
	int log_level = LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO;

	// Setup LWS logging
	if (opt_debug >= 3) {
		log_level |= LLL_DEBUG | LLL_CLIENT;
	}
	lws_set_log_level(log_level, ws_log);

	// Create new context descriptor
	ws = malloc(sizeof(ws_t));
	memset(ws, 0, sizeof(ws_t));

	ws_events_init(&ws_protocols[1]);

	memset(&info, 0, sizeof(info));
	info.port = port;
	info.protocols = ws_protocols;
	//info.extensions = lws_get_internal_extensions();

	/* Setup SSL info */
#ifdef WITH_SSL
	if (ssl_dir != NULL) {
		snprintf(cert_path, sizeof(cert_path), "%s/cert.pem", ssl_dir);
		info.ssl_cert_filepath = cert_path;

		snprintf(key_path, sizeof(key_path), "%s/privkey.pem", ssl_dir);
		info.ssl_private_key_filepath = key_path;

		log_debug(2, "SSL info: cert='%s' key='%s'", cert_path, key_path);

		info.options |= LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS;
	}
#endif

	info.gid = -1;
	info.uid = -1;
	info.user = ws;

	/* Create libwebsockets context */
	ws->context = lws_create_context(&info);
	if (ws->context == NULL) {
		log_str("ERROR: libwebsocket init failed");
		free(ws);
		return NULL;
	}

	/* Init table of document root directories */
	hk_tab_init(&ws->document_roots, sizeof(char *));

	/* Init table of aliases */
	hk_tab_init(&ws->aliases, sizeof(ws_alias_t));

	/* Init table of websocket sessions */
	hk_tab_init(&ws->sessions, sizeof(void *));

	return ws;
}


void ws_destroy(ws_t *ws)
{
	int i;

	/* Free document root directory list */
	for (i = 0; i < ws->document_roots.nmemb; i++) {
		char **p = HK_TAB_PTR(ws->document_roots, char *, i);
		free(*p);
		*p = NULL;
	}
	hk_tab_cleanup(&ws->document_roots);

	/* Destroy libwebsockets context */
	if (ws->context != NULL) {
		lws_context_destroy((struct lws_context *) ws->context);
	}

	memset(ws, 0, sizeof(ws_t));
	free(ws);
}


/*
 * HTTP directory and aliased locations
 */

void ws_add_document_root(ws_t *ws, char *dir)
{
	log_debug(2, "ws_add_document_root '%s'", dir);
	char **p = hk_tab_push(&ws->document_roots);
	*p = strdup(dir);
}


void ws_alias(ws_t *ws, char *location, ws_alias_handler_t handler, void *user_data)
{
	ws_alias_t *alias = hk_tab_push(&ws->aliases);

	if (location != NULL) {
		alias->location = strdup(location);
		alias->len = strlen(location);
	}

	alias->handler = handler;
	alias->user_data = user_data;

	log_debug(2, "ws_alias '%s'", location);
}


/*
 * WebSocket running sessions
 */

int ws_session_add(ws_t *ws, void *pss)
{
	void **ppss = NULL;
	int i;

	ws->salt++;
	ws->salt &= 0xFF;

	for (i = 0; i < ws->sessions.nmemb; i++) {
		ppss = HK_TAB_PTR(ws->sessions, void *, i);
		if (*ppss == NULL) {
			goto done;
		}
	}

	ppss = hk_tab_push(&ws->sessions);
done:
	*ppss = pss;

	return (ws->salt << 8) + (i & 0xFF);
}


void ws_session_remove(ws_t *ws, void *pss)
{
	void **ppss = NULL;
	int i;

	for (i = 0; i < ws->sessions.nmemb; i++) {
		ppss = HK_TAB_PTR(ws->sessions, void *, i);
		if (*ppss == pss) {
			*ppss = NULL;
		}
	}
}


void ws_session_foreach(ws_t *ws, ws_session_foreach_func func, void *user_data)
{
	int i;

	for (i = 0; i < ws->sessions.nmemb; i++) {
		void *pss = HK_TAB_VALUE(ws->sessions, void *, i);
		if (pss != NULL) {
			if (func != NULL) {
				func(user_data, pss);
			}
		}
	}
}


/*
 * WebSocket command handler
 */

void ws_set_command_handler(ws_t *ws, ws_command_handler_t handler, void *user_data)
{
	ws->command_handler = handler;
	ws->command_user_data = user_data;
}


void ws_call_command_handler(ws_t *ws, int argc, char **argv, buf_t *out_buf)
{
	if (ws->command_handler != NULL) {
		ws->command_handler(ws->command_user_data, argc, argv, out_buf);
	}
}
