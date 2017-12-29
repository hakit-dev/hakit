/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014-2017 Sylvain Giroudon
 *
 * HAKit Endpoints trace storage
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <sys/time.h>

#include "log.h"
#include "buf.h"
#include "endpoint.h"
#include "trace.h"


static inline uint64_t hk_trace_tstamp(void)
{
	struct timeval t;

	gettimeofday(&t, NULL);
        return (((uint64_t) t.tv_sec) * 1000) + (t.tv_usec / 1000);
}


void hk_trace_init(hk_trace_t *tr, int depth)
{
        memset(tr, 0, sizeof(hk_trace_t));

        if (depth > HK_TRACE_MAX_DEPTH) {
                log_str("WARNING: Configuring trace to %d entries is too deep. Clamping to %d entries.", depth, HK_TRACE_MAX_DEPTH);
                tr->depth = HK_TRACE_MAX_DEPTH;
        }
        else if (depth <= 0) {
                tr->depth = HK_TRACE_MAX_DEPTH;
        }
        else {
                tr->depth = depth;
        }

        tr->tab = calloc(tr->depth, sizeof(hk_trace_entry_t));

        tr->t0 = hk_trace_tstamp();
}


static int hk_trace_clear_entry(hk_trace_entry_t *entry)
{
        entry->ep = NULL;
        entry->t = 0;

        if (entry->value != NULL) {
                free(entry->value);
                entry->value = NULL;
        }

        return 1;
}


void hk_trace_clear(hk_trace_t *tr)
{
        int i;

        tr->iput = 0;

        for (i = 0; i < tr->depth; i++) {
                hk_trace_clear_entry(&tr->tab[i]);
        }
}


void hk_trace_push(hk_trace_t *tr, hk_ep_t *ep)
{
        hk_trace_entry_t *entry = &tr->tab[tr->iput++];

        hk_trace_clear_entry(entry);
        entry->ep = ep;
        entry->t = hk_trace_tstamp() - tr->t0;
        entry->value = strdup((char *) ep->value.base);

        if (tr->iput >= tr->depth) {
                tr->iput = 0;
        }

        if (tr->iput == tr->iget) {
                tr->iget++;
                if (tr->iget >= tr->depth) {
                        tr->iget = 0;
                }
        }

}


void hk_trace_dump(hk_trace_t *tr, hk_ep_t *ep, buf_t *out_buf)
{
        hk_ep_t *ep0 = NULL;
        int i;

        buf_append_fmt(out_buf, "#T0 %llu\n", tr->t0);

        i = tr->iget;
        while (i != tr->iput) {
                hk_trace_entry_t *entry = &tr->tab[i++];
                if (i >= tr->depth) {
                        i = 0;
                }

                if (entry->ep == NULL) {
                        break;
                }

                if ((ep == NULL) || (ep == entry->ep)) {
                        if (entry->ep != ep0) {
                                if (ep0 != NULL) {
                                        buf_append_str(out_buf, "\n");
                                }
                                buf_append_str(out_buf, entry->ep->obj->name);
                        }
                        buf_append_fmt(out_buf, " %llu,%s", entry->t, entry->value);
                        ep0 = entry->ep;
                }
        }

        if (ep0 != NULL) {
                buf_append_str(out_buf, "\n");
        }
}
