/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2021 Sylvain Giroudon
 *
 * LWS logging
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <libwebsockets.h>

#include "types.h"
#include "log.h"
#include "ws_log.h"


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


void ws_log_init(int debug)
{
	static int initialized = 0;
	int log_level = LLL_ERR | LLL_WARN | LLL_NOTICE;

	if (initialized) {
		return;
	}

	if (debug >= 2) {
		log_level |= LLL_INFO;
	}
	if (debug >= 3) {
		log_level |= LLL_DEBUG | LLL_HEADER | LLL_EXT | LLL_CLIENT | LLL_LATENCY;
	}
	lws_set_log_level(log_level, ws_log);

	initialized = 1;
}
