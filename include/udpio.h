#ifndef __HAKIT_UDPIO_H__
#define __HAKIT_UDPIO_H__

#include "io.h"


/*
 * UDP send to host
 */
extern int udp_send(int fd, char *addr, int port, char *buf, int size);


/*
 * UDP send to broadcast
 */
extern int udp_send_bcast(int fd, int port, char *buf, int size);


/*
 * UDP server
 */

typedef struct {
	io_channel_t chan;
	int port;
	struct sockaddr_in iremote;
} udp_srv_t;

extern void udp_srv_clear(udp_srv_t *srv);
extern int udp_srv_init(udp_srv_t *srv, int port, io_func_t func, void *user_data);
extern int udp_srv_send_to(udp_srv_t *srv, char *buf, int size, char *addr);
extern int udp_srv_send_reply(udp_srv_t *srv, char *buf, int size);
extern int udp_srv_send_bcast(udp_srv_t *srv, char *buf, int size);
extern struct sockaddr_in *udp_srv_remote(udp_srv_t *srv);
extern void udp_srv_shutdown(udp_srv_t *srv);

#endif /* __HAKIT_UDPIO_H__ */
