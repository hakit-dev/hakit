/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_IPUTILS_H__
#define __HAKIT_IPUTILS_H__

#include <netinet/in.h>

extern char *ip_addr(struct sockaddr *sa, char *buf, int size);

#endif /* __HAKIT_IPUTILS_H__ */
