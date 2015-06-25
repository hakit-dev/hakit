/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_COMM_H__
#define __HAKIT_COMM_H__

#include "sys.h"
#include "buf.h"
#include "tcpio.h"
#include "udpio.h"
#include "tab.h"
#include "command.h"

typedef void (*comm_sink_func_t)(void *user_data, char *name, char *value);


extern int comm_init(void);
extern void comm_monitor(comm_sink_func_t func, void *user_data);

extern int comm_sink_register(char *name, comm_sink_func_t func, void *user_data);
extern void comm_sink_set_local(int id);
extern void comm_sink_set_widget(int id, char *widget_name);

extern int comm_source_register(char *name, int event);
extern void comm_source_set_local(int id);
extern void comm_source_set_widget(int id, char *widget_name);
extern void comm_source_update_str(int id, char *value);
extern void comm_source_update_int(int id, int value);

#endif /* __HAKIT_COMM_H__ */
