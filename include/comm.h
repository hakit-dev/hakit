#ifndef __HAKIT_COMM_H__
#define __HAKIT_COMM_H__

#include "sys.h"
#include "buf.h"
#include "tcpio.h"
#include "udpio.h"
#include "tab.h"
#include "command.h"

/* Default HAKit communication port */
#define HAKIT_COMM_PORT 5678

typedef struct comm_s comm_t;

typedef struct {
	int id;
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
	int id;
	char *name;
	buf_t value;
	int event;
	hk_tab_t node_ids;
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
	comm_sink_func_t monitor_func;
	void *monitor_user_data;
};


extern int comm_init(comm_t *comm, int port);
extern void comm_monitor(comm_t *comm, comm_sink_func_t func, void *user_data);

extern void comm_sink_register(comm_t *comm, char *name, comm_sink_func_t func, void *user_data);
extern void comm_sink_unregister(comm_t *comm, char *name);

extern int comm_source_register(comm_t *comm, char *name, int event);
extern void comm_source_unregister(comm_t *comm, char *name);
extern void comm_source_update_str(comm_t *comm, int id, char *value);
extern void comm_source_update_int(comm_t *comm, int id, int value);
extern void comm_source_send(comm_t *comm, int id);

#endif /* __HAKIT_COMM_H__ */
