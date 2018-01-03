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

#ifndef __HAKIT_TSTAMP_H__
#define __HAKIT_TSTAMP_H__

#include <stdint.h>

extern uint64_t tstamp_ms(void);
extern int tstamp_str(char *buf, int size);

#endif /* __HAKIT_TSTAMP_H__ */
