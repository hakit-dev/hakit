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

#include "tstamp.h"
#include "log.h"
#include "buf.h"
#include "endpoint.h"
#include "trace.h"


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
        entry->t = tstamp_ms();
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


void hk_trace_dump(hk_trace_t *tr, hk_ep_t *ep, uint64_t t1, uint64_t t2, buf_t *out_buf)
{
        hk_trace_entry_t *pre = NULL;
        hk_trace_entry_t *last = NULL;
        int i;

        i = tr->iget;
        while (i != tr->iput) {
                hk_trace_entry_t *entry = &tr->tab[i++];
                if (i >= tr->depth) {
                        i = 0;
                }

                if (entry->ep == NULL) {
                        break;
                }

                if (entry->ep == ep) {
                        if ((t1 == 0) || (entry->t >= t1)) {
                                if ((t2 == 0) || (entry->t <= t2)) {
                                        if (last == NULL) {
                                                buf_append_str(out_buf, ep->obj->name);
                                        }

                                        if (pre != NULL) {
                                                if (pre != entry) {
                                                        buf_append_fmt(out_buf, " %llu,%s", t1, pre->value);
                                                }
                                                pre = NULL;
                                        }

                                        buf_append_fmt(out_buf, " %llu,%s", entry->t, entry->value);
                                        last = entry;
                                }
                                else {
                                        if (last != NULL) {
                                                buf_append_fmt(out_buf, " %llu,%s\n", t2, last->value);
                                                last = NULL;
                                        }
                                        break;
                                }
                        }
                        else {
                                pre = entry;
                        }
                }
        }

        if (last != NULL) {
                uint64_t t = tstamp_ms();
                buf_append_fmt(out_buf, " %llu,%s\n", t, last->value);
        }
}
