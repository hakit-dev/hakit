#ifndef __HTTP_H__
#define __HTTP_H__

#include "buf.h"

extern int http_recv_header(buf_t *buf, unsigned char *rbuf, int rsize, int *content_length);
extern int http_status(unsigned char *str);

#endif /* __HTTP_H__ */
