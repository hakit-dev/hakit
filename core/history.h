/*
 * HAKit - The Home Automation Kit - www.hakit.net
 * Copyright (C) 2014-2015 Sylvain Giroudon
 *
 * Signal history logging
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_HISTORY_H__
#define __HAKIT_HISTORY_H__

extern void history_init(void);
extern void history_signal_declare(int id, char *name);

extern void history_feed(int id, char *value);

extern void history_dump(FILE *f);

#endif /* __HAKIT_HISTORY_H__ */
