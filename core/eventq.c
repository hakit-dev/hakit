/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <time.h>

#include "log.h"
#include "sys.h"
#include "buf.h"
#include "comm.h"
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
static int eventq_flushing = 0;


static int eventq_flush(void);


static void eventq_timeout_stop(void)
{
	if (eventq_timeout_tag) {
		sys_remove(eventq_timeout_tag);
		eventq_timeout_tag = 0;
	}
}


static void eventq_timeout_start(int delay)
{
	log_debug(2, "eventq_timeout_start %d", delay);

	eventq_timeout_stop();

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


static void eventq_http_recv(void *user_data, char *buf, int len)
{
	int ok = 0;
	int delay = 10;
	int i;

	eventq_flushing = 0;

	if (buf != NULL) {
		log_debug(1, "eventq_http_recv len=%d", len);

		char *s2 = strchr(buf, '\n');
		if (s2 != NULL) {
			*(s2++) = '\0';
		}

		if (strcmp(buf, "0 OK") == 0) {
			log_str("Event queue: flush completed successfully");
			ok = 1;
			delay = 0;
		}
		else {
			log_str("Event queue: flush failed (reply = '%s')", buf);
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
	}
	else {
		log_str("Event queue: flush failed (HTTP error)");
	}

	if (delay > 0) {
		log_str("Event queue: Retrigering flush in %d seconds", delay);
		eventq_timeout_start(delay);
	}
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
		if (comm_wget((char *) out_buf.base, eventq_http_recv, NULL) == 0) {
			eventq_flushing = 1;

			for (i = 0; i < EVENTQ_DEPTH; i++) {
				eventq_t *e = &eventq_buf[i];
				if (e->state == EVENTQ_ST_NEW) {
					e->state = EVENTQ_ST_SENDING;
				}
			}
		}
		else {
			log_str("Event queue: connection failure - retrying in 120s ...");
			eventq_timeout_start(120);
		}
	}

	buf_cleanup(&out_buf);

	return 0;
}


void eventq_push(char *str)
{
	eventq_t *e;

	if (eventq_url == NULL) {
		log_str("ERROR: No eventq URL defined");
		return;
	}

	log_debug(2, "eventq_push '%s'", str);

	eventq_timeout_stop();

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

	if (!eventq_flushing) {
		eventq_timeout_start(1);
	}
}



void eventq_init(char *url)
{
	if (eventq_url != NULL) {
		free(eventq_url);
	}
	eventq_url = strdup(url);
}
