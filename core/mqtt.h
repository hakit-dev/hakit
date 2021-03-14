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

#ifdef WITH_MQTT

/* MQTT topic update handler */
typedef void (*mqtt_update_func_t)(void *user_data, char *name, char *value);

/* MQTT instance descriptor */
typedef struct __mqtt_s mqtt_t;

#ifndef __HAKIT_MQTT_C__
/* MQTT instance descriptor is opaque from outside MQTT protocol implementation lib */
struct __mqtt_s {};
#endif


/* MQTT instance functions */
extern int mqtt_init(mqtt_t *mqtt, char *certs,
		     mqtt_update_func_t update_func, void *user_data);
extern void mqtt_shutdown(mqtt_t *mqtt);

extern int mqtt_connect(mqtt_t *mqtt, char *broker);
extern int mqtt_publish(mqtt_t *mqtt, char *name, char *value, int retain);
extern int mqtt_subscribe(mqtt_t *mqtt, char *name);

#endif /* WITH_MQTT */

#endif /* __HAKIT_MQTT_H__ */
