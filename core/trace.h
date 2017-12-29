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

#ifndef __HAKIT_TRACE_H__
#define __HAKIT_TRACE_H__

#include <stdint.h>

#include "endpoint.h"

#define HK_TRACE_MAX_DEPTH 1000

typedef struct {
        hk_ep_t *ep;
        uint64_t t;
        char *value;
} hk_trace_entry_t;


typedef struct {
        int depth;
        uint64_t t0;
        int iput, iget;
        hk_trace_entry_t *tab;
} hk_trace_t;

extern void hk_trace_init(hk_trace_t *tr, int depth);
extern void hk_trace_push(hk_trace_t *tr, hk_ep_t *ep);
extern void hk_trace_dump(hk_trace_t *tr, hk_ep_t *ep, buf_t *out_buf);

#endif /* __HAKIT_TRACE_H__ */
