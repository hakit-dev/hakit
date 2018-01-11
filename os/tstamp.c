/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * Time stamp generator
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

#include "tstamp.h"


static uint64_t _tstamp_t0 = 0;


static inline uint64_t tstamp_ms_abs(void)
{
	struct timeval t;

	gettimeofday(&t, NULL);
        return (((uint64_t) t.tv_sec) * 1000) + (t.tv_usec / 1000);
}


uint64_t tstamp_t0(void)
{
        if (_tstamp_t0 == 0) {
                _tstamp_t0 = tstamp_ms_abs();
        }
        return _tstamp_t0;
}


uint64_t tstamp_ms(void)
{
        return tstamp_ms_abs() - tstamp_t0();
}


int tstamp_str(char *buf, int size)
{
	struct timeval t;
	int len;

	gettimeofday(&t, NULL);

	len = strftime(buf, size, "[%Y-%m-%d %H:%M:%S", localtime(&t.tv_sec));
	len += snprintf(buf+len, size-len, ".%03ld] ", t.tv_usec/1000);

        return len;
}
