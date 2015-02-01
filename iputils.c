#include <stdio.h>
#include <netinet/in.h>

#include "iputils.h"


char *ip_addr(char *str, struct sockaddr_in *addr)
{
	static char buf[32];

	if ( str == NULL )
		str = buf;

	sprintf(str, "%d.%d.%d.%d:%d",
		(int) (addr->sin_addr.s_addr >>  0) & 0xFF,
		(int) (addr->sin_addr.s_addr >>  8) & 0xFF,
		(int) (addr->sin_addr.s_addr >> 16) & 0xFF,
		(int) (addr->sin_addr.s_addr >> 24) & 0xFF,
		ntohs(addr->sin_port) & 0xFFFF);

	return str;
}
