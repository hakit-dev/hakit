#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <time.h>

#include "log.h"
#include "sys.h"
#include "buf.h"
#include "tcpio.h"
#include "http.h"
#include "eventq.h"

#define EVENTQ_DEPTH 16


typedef enum {
	EVENTQ_NONE=0,
	EVENTQ_STR,
	EVENTQ_STRIPPED,
	EVENTQ_N
} eventq_type_t;

typedef enum {
	EVENTQ_ST_NEW=0,
	EVENTQ_ST_SENDING,
	EVENTQ_ST_SENT,
} eventq_state_t;

typedef struct {
	unsigned long t;
	eventq_type_t type;
	char *str;
	eventq_state_t state;
} eventq_t;

static eventq_t eventq_buf[EVENTQ_DEPTH];
static int eventq_index = 0;
static char *eventq_url = NULL;
static sys_tag_t eventq_timeout_tag = 0;
static tcp_sock_t eventq_http_sock;
static buf_t eventq_http_buf;


static int eventq_flush(void);


static void eventq_timeout_start(int delay)
{
	log_debug(2, "eventq_timeout_start %d", delay);

	if (eventq_timeout_tag) {
		sys_remove(eventq_timeout_tag);
		eventq_timeout_tag = 0;
	}

	if (delay > 0) {
		eventq_timeout_tag = sys_timeout(1000*delay, (sys_func_t) eventq_flush, NULL);
	}
}


static int eventq_str(buf_t *out_buf, char *sep)
{
	char str[16];
	int i, index;
	int count = 0;

	index = eventq_index;
	for (i = 0; i < EVENTQ_DEPTH; i++) {
		eventq_t *e = &eventq_buf[index++];
		if (index >= EVENTQ_DEPTH) {
			index = 0;
		}

		if ((e->type != EVENTQ_NONE) && (e->state == EVENTQ_ST_NEW)) {
			if (count > 0) {
				buf_append_str(out_buf, sep);
			}
			count++;

			snprintf(str, sizeof(str), "%08lX-", e->t);
			buf_append_str(out_buf, str);

			if (e->type == EVENTQ_STR) {
				buf_append_str(out_buf, e->str);
			}
			else {
				buf_append_str(out_buf, "...");
			}
		}
	}

	return count;
}


static void http_client_recv(unsigned char *rbuf, int rsize)
{
	int roffset;
	int content_size;
	int i;

	log_debug_data((unsigned char *) rbuf, rsize);

	roffset = http_recv_header(&eventq_http_buf, rbuf, rsize, &content_size);
	if (roffset > 0) {
		int status = http_status(eventq_http_buf.base);
		int ok = 0;
		int delay = 10;

		if (status == 200) {
			char *s = (char *) &rbuf[roffset];
			char *s2 = strchr(s, '\n');
			if (s2 != NULL) {
				*(s2++) = '\0';
			}

			log_debug(1, "http_client_recv rsize=%d roffset=%d", rsize, roffset);
			if (strcmp(s, "0 OK") == 0) {
				log_str("Event queue: flush completed successfully");
				ok = 1;
				delay = 0;
			}
			else {
				log_str("Event queue: flush failed (reply = '%s')", s);
			}
		}
		else {
			log_str("Event queue: flush failed (HTTP status = %d)");
		}

		for (i = 0; i < EVENTQ_DEPTH; i++) {
			eventq_t *e = &eventq_buf[i];
			switch (e->state) {
			case EVENTQ_ST_NEW:
				delay = 1;
				break;
			case EVENTQ_ST_SENDING:
				e->state = ok ? EVENTQ_ST_SENT : EVENTQ_ST_NEW;
				break;
			default:
				break;
			}
		}

		if (delay > 0) {
			log_str("Retrigering event queue flush in %d seconds", delay);
			eventq_timeout_start(delay);
		}
	}
}


static void http_client_handler(tcp_sock_t *tcp_sock, tcp_io_t io, char *rbuf, int rsize)
{
	log_debug(1, "http_client_handler %d %d", io, rsize);

	switch (io) {
	case TCP_IO_CONNECT:
		break;
	case TCP_IO_DATA:
		if (rsize > 0) {
			http_client_recv((unsigned char *) rbuf, rsize);
		}
		break;
	case TCP_IO_HUP:
		tcp_sock_clear(tcp_sock);
		break;
	}
}


static int http_client_get(char *url)
{
	int ret = 0;
	int port = 80;
	char *location;
	char *sport;

	if (strncmp(url, "http://", 7) == 0) {
		url += 7;
	}

	location = strchr(url, '/');
	if (location != NULL) {
		*location = '\0';
	}

	sport = strchr(url, ':');
	if (sport != NULL) {
		*sport = '\0';
		port = atoi(sport+1);
		*sport = ':';
	}

	tcp_sock_shutdown(&eventq_http_sock);
	tcp_sock_clear(&eventq_http_sock);
	buf_cleanup(&eventq_http_buf);

	if (tcp_sock_connect(&eventq_http_sock, url, port, http_client_handler) > 0) {
		buf_t req;

		buf_init(&req);
		buf_append_str(&req, "GET ");
		if (location != NULL) {
			*location = '/';
			buf_append_str(&req, location);
			*location = '\0';
		}
		else {
			buf_append_str(&req, "/");
		}
		buf_append_str(&req, (char *) " HTTP/1.1\r\nHost: ");
		buf_append_str(&req, url);
		buf_append_str(&req, (char *) "\r\nConnection: Close\r\n\r\n");

		log_debug_data(req.base, req.len);
		tcp_sock_write(&eventq_http_sock, (char *) req.base, req.len);

		buf_cleanup(&req);
	}
	else {
		eventq_http_sock.chan.fd = -1;
		ret = -1;
	}

	if (location != NULL) {
		*location = '/';
	}

	return ret;
}


static int eventq_flush(void)
{
	buf_t out_buf;
	int i;

	buf_init(&out_buf);
	buf_append_str(&out_buf, eventq_url);
	buf_append_str(&out_buf, "?event=");

	i = eventq_str(&out_buf, ",");
	log_debug(2, "eventq_flush index=%d count=%d", eventq_index, i);

	if (i > 0) {
		log_debug_data(out_buf.base, out_buf.len);

		log_str("Event queue: flushing %d event%s", i, (i > 1) ? "s":"");

		/* Send event to URL */
		if (http_client_get((char *) out_buf.base) == 0) {
			for (i = 0; i < EVENTQ_DEPTH; i++) {
				eventq_t *e = &eventq_buf[i];
				if (e->state == EVENTQ_ST_NEW) {
					e->state = EVENTQ_ST_SENDING;
				}
			}
		}
		else {
			log_str("Failed to flush event queue: retrying in 120s ...");
			eventq_timeout_start(120);
		}
	}

	buf_cleanup(&out_buf);

	return 0;
}


void eventq_push(char *str)
{
	eventq_t *e;

	log_debug(2, "eventq_push '%s'", str);

	eventq_timeout_start(0);

	e = &eventq_buf[eventq_index++];
	e->t = time(NULL) & 0xFFFFFFFF;
	e->type = EVENTQ_STR;
	e->str = strdup(str);
	e->state = EVENTQ_ST_NEW;

	if (eventq_index >= EVENTQ_DEPTH) {
		eventq_index = 0;
	}
	e = &eventq_buf[eventq_index];
	if (e->type != EVENTQ_NONE) {
		e->type = EVENTQ_STRIPPED;
	}
	if (e->str != NULL) {
		free(e->str);
		e->str = NULL;
	}

	if (eventq_http_sock.chan.fd < 0) {
		eventq_timeout_start(1);
	}
}



void eventq_init(char *url)
{
	memset(&eventq_buf, 0, sizeof(eventq_buf));
	eventq_index = 0;

	if (eventq_url != NULL) {
		free(eventq_url);
	}
	eventq_url = strdup(url);

	tcp_sock_clear(&eventq_http_sock);
	buf_init(&eventq_http_buf);
}
