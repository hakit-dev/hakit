#include <stdio.h>
#include <netinet/in.h>

#include "iputils.h"


char *ip_addr(char *str, struct sockaddr_in *addr)
{
	unsigned long addr_v = ntohl(addr->sin_addr.s_addr);
	static char buf[32];

	if ( str == NULL )
		str = buf;

	sprintf(str, "%lu.%lu.%lu.%lu:%d",
		(addr_v >> 24) & 0xFF, (addr_v >> 16) & 0xFF, (addr_v >> 8) & 0xFF, addr_v & 0xFF,
		ntohs(addr->sin_port) & 0xFFFF);

	return str;
}
