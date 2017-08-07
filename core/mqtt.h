/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * MQTT Connectivity Protocol
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_MQTT_H__
#define __HAKIT_MQTT_H__

#include <mqueue.h>
#include "sys.h"
#include "mosquitto.h"

/* MQTT default settings */
#define MQTT_DEFAULT_PORT 1883
#define MQTT_DEFAULT_SSL_PORT 8883
#define MQTT_DEFAULT_KEEPALIVE 60

/* MQTT current settings */
extern char *mqtt_user;
extern char *mqtt_host;
extern int mqtt_port;
extern int mqtt_keepalive;
extern int mqtt_qos;


/* MQTT instance descriptor */
typedef void (*mqtt_update_func_t)(void *user_data, char *name, char *value);

typedef enum {
	MQTT_ST_DISCONNECTED=0,
	MQTT_ST_CONNECTED
} mqtt_state_t;

typedef struct {
	struct mosquitto *mosq;
	mqtt_update_func_t update_func;
	void *user_data;
	mqtt_state_t state;
	mqd_t mq;
	sys_tag_t mq_tag;
} mqtt_t;


/* MQTT instance functions */
extern int mqtt_init(mqtt_t *mqtt, char *ssl_dir,
		     mqtt_update_func_t update_func, void *user_data);
extern int mqtt_publish(mqtt_t *mqtt, char *name, char *value, int retain);
extern int mqtt_subscribe(mqtt_t *mqtt, char *name);

#endif /* __HAKIT_MQTT_H__ */
