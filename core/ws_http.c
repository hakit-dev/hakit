/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2021 Sylvain Giroudon
 *
 * HTTP server
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
#include <libwebsockets.h>

#include "log.h"
#include "buf.h"
#include "tab.h"
#include "mime.h"
#include "ws_server.h"
#include "ws_io.h"
#include "ws_auth.h"
#include "ws_http.h"


#define SERVER_NAME "HAKit"

struct per_session_data__http {
	FILE *f;
	buf_t rsp;
	int offset;
	unsigned char tx_buffer[4096];
};


static char *search_file(ws_server_t *server, char *uri)
{
        char *subdir = NULL;
        int subdir_len = 0;
        char *file_path = NULL;
	int i;

        log_debug(3, "search_file('%s')", uri);

        char *s = uri;
        if (*s == '/') {
                s++;
        }
        while ((*s != '\0') && (*s != '/')) {
                s++;
        }
        if (*s != '\0') {
                subdir_len = s - uri;
                subdir = malloc(subdir_len+1);
                memcpy(subdir, uri, subdir_len);
                subdir[subdir_len] = '\0';
                uri = s;
        }

        int uri_len = strlen(uri);

        log_debug(3, "  subdir='%s' uri='%s'", subdir, uri);

	for (i = 0; (file_path == NULL) && (i < server->document_roots.nmemb); i++) {
		char *dir = HK_TAB_VALUE(server->document_roots, char *, i);
                int file_path_len = strlen(dir);
		int file_path_size = file_path_len + subdir_len + uri_len + 20;
		file_path = malloc(file_path_size);

		strcpy(file_path, dir);

                if (subdir != NULL) {
                        strcpy(file_path+file_path_len, subdir);
                        file_path_len += subdir_len;
                }

                if (file_path[file_path_len-1] != '/') {
                        file_path[file_path_len++] = '/';
                }
                strcpy(file_path+file_path_len, "ui");
		file_path_len += 2;

		strcpy(file_path+file_path_len, uri);
		file_path_len += uri_len;

                log_debug(3, "  file_path='%s'", file_path);

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
		}
                else {
                        free(file_path);
                        file_path = NULL;
                }
	}

        if (subdir != NULL) {
                free(subdir);
                subdir = NULL;
        }

        if (file_path != NULL) {
                log_debug(3, "  -> '%s'", file_path);
        }
        else {
                log_debug(3, "  -> Not found");
        }

        return file_path;
}


static void show_connection_info(struct lws *wsi)
{
	char name[100], rip[50];
	char *user_agent = NULL;
	int len;

	/* Get peer address */
	lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi), name, sizeof(name), rip, sizeof(rip));

	/* Show user agent */
	len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_USER_AGENT);
	if (len > 0) {
		int size = len + 1;
		user_agent = malloc(size);
		lws_hdr_copy(wsi, user_agent, size, WSI_TOKEN_HTTP_USER_AGENT);
	}
	else {
		user_agent = strdup("?");
	}

	log_str("HTTP connect from %s (%s): %s", name, rip, user_agent);

	if (user_agent != NULL) {
		free(user_agent);
	}
}


static int ws_http_request(ws_server_t *server,
			   struct lws *wsi,
			   struct per_session_data__http *pss,
			   char *uri, size_t len)
{
	char *username = NULL;
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

	show_connection_info(wsi);

	if (len < 1) {
		lws_return_http_status(wsi, HTTP_STATUS_BAD_REQUEST, NULL);
		goto try_to_reuse;
	}

	/* Clear data source settings */
	pss->f = NULL;
	buf_init(&pss->rsp);
	pss->offset = 0;

	/* Setup reply buffering */
	p = pss->tx_buffer + LWS_SEND_BUFFER_PRE_PADDING;
	end = p + sizeof(pss->tx_buffer) - LWS_SEND_BUFFER_PRE_PADDING;

	//ws_show_http_token(wsi);

	/* Check authorization */
	if (ws_auth_check(wsi, &username)) {
		if (username != NULL) {
			log_str("HTTP authentication accepted for user '%s'", username);
		}
	}
	else {
		char *str = "Basic realm=\"HAKit HTTP Server\"";

		log_str("Requesting HTTP authentication");

		if (lws_add_http_header_status(wsi, HTTP_STATUS_UNAUTHORIZED, &p, end)) {
			goto done;
		}

		if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_WWW_AUTHENTICATE,
						 (unsigned char *) str, strlen(str),
						 &p, end)) {
			goto done;
		}

		goto finalize;
	}

	/* If a legal POST URL, let it continue and accept data */
	if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI)) {
		log_debug(2, "=> POST");
		return 0;
	}

        /* Replace URI prefix with declared aliases */
        for (i = 0; (i < server->aliases.nmemb) && (file_path == NULL); i++) {
                ws_alias_t *alias = HK_TAB_PTR(server->aliases, ws_alias_t, i);
                if ((alias->location != NULL) && (alias->dir != NULL)) {
                        if (strncmp(alias->location, uri, alias->len) == 0) {
                                char *uri_base = &uri[alias->len];
                                int size = strlen(alias->dir) + strlen(uri_base) + 12;
                                file_path = malloc(size);
                                int file_path_len = snprintf(file_path, size, "%s%s", alias->dir, uri_base);

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

                                log_debug(2, "HTTP alias '%s' matched: '%s' => '%s'", alias->location, uri, file_path);
                        }
                }
        }

        if (file_path == NULL) {
		/* Search file among root directory list */
		file_path = search_file(server, uri);
                if (file_path == NULL) {
                        int len = strlen(uri);
                        char uri2[len+2];
                        memcpy(uri2, uri, len);
                        uri2[len++] = '/';
                        uri2[len] = '\0';
                        file_path = search_file(server, uri2);
                }

		if (file_path == NULL) {
			log_str("HTTP ERROR: No path found for '%s'", uri);
			lws_return_http_status(wsi, HTTP_STATUS_NOT_FOUND, NULL);
			goto failed;
		}
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

	/*
	 * Construct HTTP header.
	 * Notice we use the APIs to build the header, which
	 * will do the right thing for HTTP 1/1.1 and HTTP2
	 * depending on what connection it happens to be working
	 * on
	 */
	if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end)) {
		goto done;
	}
	if (mimetype != NULL) {
		if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
						 (unsigned char *) mimetype, strlen(mimetype),
						 &p, end)) {
			goto done;
		}
	}

finalize:
	if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_SERVER,
					 (unsigned char *) SERVER_NAME, strlen(SERVER_NAME),
					 &p, end)) {
		goto done;
	}

	if (lws_add_http_header_content_length(wsi, content_length, &p, end)) {
		goto done;
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


static int ws_http_body(struct lws *wsi,
			void *in, size_t len)
{
	log_debug(2, "ws_http_body: %d bytes", (int) len);
	log_debug_data((unsigned char *) in, len);

	return 0;
}


static int ws_http_body_completion(struct lws *wsi)
{
	log_debug(2, "ws_http_body_completion");

	lws_return_http_status(wsi, HTTP_STATUS_OK, NULL);
	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;
}


static int ws_http_file_completion(struct lws *wsi)
{
	log_debug(2, "ws_http_file_completion");

	if (lws_http_transaction_completed(wsi)) {
		return -1;
	}
	return 0;
}


static int ws_http_writeable(struct lws *wsi,
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
	ws_server_t *server = lws_context_user(context);
	struct per_session_data__http *pss = (struct per_session_data__http *) user;
	struct lws_pollargs *pa = (struct lws_pollargs *) in;
	int ret = 0;

	switch (reason) {
	case LWS_CALLBACK_CLOSED_HTTP:
		log_debug(3, "ws_http_callback LWS_CALLBACK_CLOSED_HTTP");
		break;
	case LWS_CALLBACK_HTTP:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP");
		ret = ws_http_request(server, wsi, pss, in, len);
		break;
	case LWS_CALLBACK_HTTP_BODY:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_BODY");
		ret = ws_http_body(wsi, in, len);
		break;
	case LWS_CALLBACK_HTTP_BODY_COMPLETION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_BODY_COMPLETION");
		ret = ws_http_body_completion(wsi);
		break;
	case LWS_CALLBACK_HTTP_FILE_COMPLETION:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_FILE_COMPLETION");
		ret = ws_http_file_completion(wsi);
		break;
	case LWS_CALLBACK_HTTP_WRITEABLE:
		log_debug(3, "ws_http_callback LWS_CALLBACK_HTTP_WRITEABLE");
		ret = ws_http_writeable(wsi, pss);
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
		ws_poll(context, pa);
		break;
	case LWS_CALLBACK_DEL_POLL_FD:
		log_debug(3, "ws_http_callback LWS_CALLBACK_DEL_POLL_FD %d", pa->fd);
		ws_poll_remove(pa);
		break;
	case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
		log_debug(3, "ws_http_callback LWS_CALLBACK_CHANGE_MODE_POLL_FD %d %02X", pa->fd, pa->events);
		ws_poll(context, pa);
		break;

	default:
		log_debug(3, "ws_http_callback reason=%d", reason);
		break;
	}

	return ret;
}


void ws_http_init(struct lws_protocols *protocol)
{
	protocol->name = "http-server";
	protocol->callback = ws_http_callback;
	protocol->per_session_data_size = sizeof(struct per_session_data__http);
	protocol->rx_buffer_size = 0;
}
