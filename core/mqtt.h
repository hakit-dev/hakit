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

#include "mosquitto.h"

typedef void (*mqtt_update_func_t)(void *user_data, char *name, char *value);

typedef struct {
	struct mosquitto *mosq;
	mqtt_update_func_t update_func;
	void *user_data;
} mqtt_t;

extern char *mqtt_user;
extern char *mqtt_host;
extern int mqtt_port;
extern int mqtt_keepalive;
extern int mqtt_qos;

extern int mqtt_init(mqtt_t *mqtt, char *ssl_dir,
		     mqtt_update_func_t update_func, void *user_data);
extern int mqtt_publish(mqtt_t *mqtt, char *name, char *value, int retain);
extern int mqtt_subscribe(mqtt_t *mqtt, char *name);

#endif /* __HAKIT_MQTT_H__ */
