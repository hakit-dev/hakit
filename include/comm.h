#ifndef __HAKIT_COMM_H__
#define __HAKIT_COMM_H__

#include "sys.h"
#include "buf.h"
#include "tcpio.h"
#include "udpio.h"
#include "command.h"

typedef struct comm_s comm_t;

typedef struct {
	char *name;
	tcp_sock_t tcp_sock;
	int connect_attempts;
	sys_tag_t timeout_tag;
	comm_t *comm;
	command_t *cmd;
} comm_node_t;


typedef void (*comm_sink_func_t)(void *user_data, char *name, char *value);

typedef struct {
	char *name;              /* Sink name */
	buf_t value;
	comm_sink_func_t func;   /* Handler to call when sink is updated */
	void *user_data;
} comm_sink_t;


typedef struct {
	char *name;
	buf_t value;
	int event;
	comm_node_t **nodes;
	int nnodes;
} comm_source_t;


struct comm_s {
	udp_srv_t udp_srv;
	tcp_srv_t tcp_srv;
	comm_node_t *nodes;
	int nnodes;
	comm_sink_t *sinks;
	int nsinks;
	comm_source_t *sources;
	int nsources;
	sys_tag_t advertise_tag;
	io_channel_t chan_stdin;
};


extern int comm_init(comm_t *comm, int port);

extern int comm_sink_register(comm_t *comm, char *name, comm_sink_func_t func, void *user_data);
extern void comm_sink_unregister(comm_t *comm, char *name);

extern comm_source_t *comm_source_register(comm_t *comm, char *name, int event);
extern void comm_source_unregister(comm_t *comm, char *name);
extern void comm_source_update_str(comm_source_t *source, char *value);
extern void comm_source_update_int(comm_source_t *source, int value);
extern void comm_source_send(comm_source_t *source);

#endif /* __HAKIT_COMM_H__ */
