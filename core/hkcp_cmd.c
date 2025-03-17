/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2021 Sylvain Giroudon
 *
 * HAKit Connectivity Protocol - command processing
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>

#include "buf.h"
#include "tab.h"
#include "tstamp.h"
#include "endpoint.h"
#include "hakit_version.h"
#include "hkcp.h"
#include "hkcp_cmd.h"


static void hkcp_command_set(int argc, char **argv, buf_t *out_buf)
{
	int i;

	for (i = 1; i < argc; i++) {
		char *name = argv[i];
		char *value = strchr(name, '=');

		if (value != NULL) {
			hk_sink_t *sink;

			*(value++) = '\0';

			sink = hk_sink_retrieve_by_name(name);
			if (sink != NULL) {
				/* Update sink value and invoke sink event callback */
				hk_sink_update(sink, value);
			}
			else {
				/* Send back error message */
				buf_append_str(out_buf, ".ERROR: Unknown sink: ");
				buf_append_str(out_buf, name);
				buf_append_str(out_buf, "\n");
			}
		}
		else {
			buf_append_str(out_buf, ".ERROR: Syntax error in command: ");
			buf_append_str(out_buf, name);
			buf_append_str(out_buf, "\n");
		}
	}
}


static int hkcp_command_get_source(buf_t *out_buf, hk_source_t *source)
{
	hk_ep_dump(HK_EP(source), out_buf);
	return 1;
}


static int hkcp_command_get_sink(buf_t *out_buf, hk_sink_t *sink)
{
	hk_ep_dump(HK_EP(sink), out_buf);
	return 1;
}


static void hkcp_command_get(int argc, char **argv, buf_t *out_buf)
{
	int i;

	if (argc > 1) {
		for (i = 1; i < argc; i++) {
                        hk_source_t *source = hk_source_retrieve_by_name(argv[i]);
			hk_ep_dump(HK_EP(source), out_buf);
		}
		for (i = 1; i < argc; i++) {
                        hk_sink_t *sink = hk_sink_retrieve_by_name(argv[i]);
			hk_ep_dump(HK_EP(sink), out_buf);
		}
	}
	else {
                hk_source_foreach((hk_ep_foreach_func_t) hkcp_command_get_source, out_buf);
                hk_sink_foreach((hk_ep_foreach_func_t) hkcp_command_get_sink, out_buf);
	}

	buf_append_str(out_buf, ".\n");
}


static int hkcp_command_nodes_source(buf_t *out_buf, hk_source_t *source)
{
        buf_append_str(out_buf, " ");
        hk_ep_append_name(HK_EP(source), out_buf);
        return 1;
}

static void hkcp_command_nodes(buf_t *out_buf, hkcp_t *hkcp)
{
	int i;

	for (i = 0; i < hkcp->nodes.nmemb; i++) {
		hkcp_node_t *node = HK_TAB_VALUE(hkcp->nodes, hkcp_node_t *, i);

		if (node != NULL) {
			buf_append_str(out_buf, node->name);
                        hk_source_foreach((hk_ep_foreach_func_t) hkcp_command_nodes_source, out_buf);
			buf_append_str(out_buf, "\n");
		}
	}

	buf_append_str(out_buf, ".\n");
}


typedef struct {
        hkcp_t *hkcp;
        buf_t *out_buf;
} hkcp_command_sources_ctx_t;


static int hkcp_command_sources_dump(hkcp_command_sources_ctx_t *ctx, hk_source_t *source)
{
        if (hk_source_is_public(source)) {
                hk_ep_append_name(HK_EP(source), ctx->out_buf);

                buf_append_str(ctx->out_buf, " \"");
                hk_ep_append_value(HK_EP(source), ctx->out_buf);
                buf_append_str(ctx->out_buf, "\"");

                hkcp_node_dump(ctx->hkcp, source, ctx->out_buf);

                buf_append_str(ctx->out_buf, "\n");
        }

        return 1;
}


static void hkcp_command_sources(buf_t *out_buf, hkcp_t *hkcp)
{
        hkcp_command_sources_ctx_t ctx = {
                .hkcp = hkcp,
                .out_buf = out_buf,
        };

        hk_source_foreach((hk_ep_foreach_func_t) hkcp_command_sources_dump, &ctx);
	buf_append_str(out_buf, ".\n");
}


static int hkcp_command_sinks_dump(buf_t *out_buf, hk_sink_t *sink)
{
        if (hk_sink_is_public(sink)) {
                hk_ep_append_name(HK_EP(sink), out_buf);
                buf_append_str(out_buf, " \"");
                hk_ep_append_value(HK_EP(sink), out_buf);
                buf_append_str(out_buf, "\"\n");
        }

        return 1;
}


static void hkcp_command_sinks(buf_t *out_buf)
{
        hk_sink_foreach((hk_ep_foreach_func_t) hkcp_command_sinks_dump, out_buf);
	buf_append_str(out_buf, ".\n");
}


static int hkcp_command_watch_source(buf_t *out_buf, hk_source_t *source)
{
        buf_append_str(out_buf, "!");
        hk_ep_append_name(HK_EP(source), out_buf);
        buf_append_str(out_buf, "=");
        hk_ep_append_value(HK_EP(source), out_buf);
        buf_append_str(out_buf, "\n");

        return 1;
}


void hkcp_command_watch(int argc, char **argv, buf_t *out_buf, int *pwatch)
{
	int err = 0;

	if (argc > 1) {
		if (argc == 2) {
			if ((strcmp(argv[1], "0") == 0) || (strcmp(argv[1], "off") == 0)) {
				*pwatch = 0;
			}
			else if ((strcmp(argv[1], "1") == 0) || (strcmp(argv[1], "on") == 0)) {
				*pwatch = 1;
			}
			else {
				err = 1;
			}
		}
		else {
			err = 1;
		}
	}

	if (err) {
		buf_append_str(out_buf, ".ERROR: watch: Syntax error\n");
	}
	else {
                buf_append_str(out_buf, ".\n");
                if (*pwatch) {
                        hk_source_foreach((hk_ep_foreach_func_t) hkcp_command_watch_source, out_buf);
                }
	}
}


void hkcp_command(hkcp_t *hkcp, int argc, char **argv, buf_t *out_buf)
{
	if (strcmp(argv[0], "set") == 0) {
		hkcp_command_set(argc, argv, out_buf);
	}
	else if (strcmp(argv[0], "get") == 0) {
		hkcp_command_get(argc, argv, out_buf);
	}
	else if (strcmp(argv[0], "nodes") == 0) {
		hkcp_command_nodes(out_buf, hkcp);
	}
	else if (strcmp(argv[0], "sinks") == 0) {
		hkcp_command_sinks(out_buf);
	}
	else if (strcmp(argv[0], "sources") == 0) {
		hkcp_command_sources(out_buf, hkcp);
	}
	else if (strcmp(argv[0], "props") == 0) {
		buf_append_str(out_buf, "VERSION: " HAKIT_VERSION "\n");
		buf_append_str(out_buf, "ARCH: " ARCH "\n");
		buf_append_fmt(out_buf, "T0: %llu\n", tstamp_t0());
		buf_append_fmt(out_buf, "TRACE_DEPTH: %d\n", hk_endpoints_get_trace_depth());
		buf_append_str(out_buf, ".\n");
	}
	else {
		buf_append_str(out_buf, ".ERROR: Unknown command: ");
		buf_append_str(out_buf, argv[0]);
		buf_append_str(out_buf, "\n");
	}
}
