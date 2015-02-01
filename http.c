#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "buf.h"
#include "http.h"


int http_recv_header(buf_t *buf, unsigned char *rbuf, int rsize, int *content_length)
{
	int crlf = 0;
	int i, ii;

	log_debug(2, "http_client_recv_header %d", rsize);

	ii = 0;
	for (i = 0; i < rsize; i++) {
		if (rbuf[i] == '\n') {
			if ((i > 0) && (rbuf[i-1] == '\r')) {
				int i1 = i + 1;
				unsigned char *lbuf = &rbuf[ii];
				int llen = i1 - ii;

				crlf++;
				buf_append(buf, lbuf, llen);
				//log_debug(2, "http_recv_header: CRLF at position %d (%d): ii=%d", i, crlf, ii);
				ii = i1;

				if (crlf == 2) {
					i++;
					log_debug(2, "End of HTTP header detected at position %d", i);
					break;
				}
				else {
					if (content_length != NULL) {
						if (memcmp(lbuf, "Content-Length:", 15) == 0) {
							*content_length = atoi((char *) &lbuf[15]);
						}
					}
				}
			}
			else {
				crlf = 0;
			}
		}
		else if (rbuf[i] != '\r') {
			crlf = 0;
		}
	}

	if (crlf >= 2) {
		return i;
	}

	return 0;
}


int http_status(unsigned char *str)
{
	int status = -1;
	unsigned char *s;
	unsigned char c;

	while (*str > ' ') {
		str++;
	}
	if (*str == ' ') {
		str++;
		s = str;
		while (*s > ' ') {
			s++;
		}
		c = *s;
		*s = '\0';
		status = atoi((char *) str);
		*s = c;
	}

	return status;
}
