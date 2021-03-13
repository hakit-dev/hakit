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

#ifndef __HAKIT_HKCP_CMD_H__
#define __HAKIT_HKCP_CMD_H__

#include "hkcp.h"

extern void hkcp_command(hkcp_t *hkcp, int argc, char **argv, buf_t *out_buf);
extern void hkcp_command_watch(int argc, char **argv, buf_t *out_buf, int *pwatch);

#endif /* __HAKIT_HKCP_CMD_H__ */
