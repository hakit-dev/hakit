/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "mosquitto.h"

#include "options.h"
#include "log.h"
#include "mqtt.h"


#define MQTT_PORT 1883
#define MQTT_SSL_PORT 8883


char *mqtt_user = NULL;
char *mqtt_host = NULL;
int mqtt_port = 0;
int mqtt_keepalive = 60;
int mqtt_qos = 0;


static void mqtt_on_connect(struct mosquitto *mosq, void *obj, int rc)
{
	log_debug(2, "MQTT connected: rc=%d", rc);
}


static void mqtt_on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
	log_debug(2, "MQTT disconnected: rc=%d", rc);
}


static void mqtt_on_publish(struct mosquitto *mosq, void *obj, int mid)
{
	log_debug(2, "MQTT published: mid=%d", mid);
}


static void mqtt_on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	log_debug(2, "MQTT message: ");
}


static void mqtt_on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	log_debug(2, "MQTT subscribed: mid=%d", mid);
}


static void mqtt_on_log(struct mosquitto *mosq, void *obj, int level, const char *str)
{
	//mqtt_t *mqtt = obj;
	char *tag;

	switch (level) {
	case MOSQ_LOG_INFO:    tag = "INFO"; break;
	case MOSQ_LOG_NOTICE:  tag = "NOTICE"; break;
	case MOSQ_LOG_WARNING: tag = "WARNING"; break;
	case MOSQ_LOG_ERR:     tag = "ERROR"; break;
	case MOSQ_LOG_DEBUG:   tag = (opt_debug > 0) ? "DEBUG":NULL; break;
	default:               tag = NULL; break;
	}

	if (tag != NULL) {
		log_str("MQTT [%s] %s", tag, str);
	}
}


int mqtt_init(mqtt_t *mqtt, char *ssl_dir, 
	      mqtt_update_func_t update_func, void *user_data)
{
	int major, minor, revision;
	int port;
	char id[128] = "hakit/";
	int ret;

	memset(mqtt, 0, sizeof(mqtt_t));

	mosquitto_lib_version(&major, &minor, &revision);
	log_str("Initialising MQTT: Mosquitto %d.%d.%d", major, minor, revision);

	ret = mosquitto_lib_init();
	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Cannot init MQTT: err=%d", ret);
		return -1;
	}

	int len = strlen(id);
	gethostname(id+len, sizeof(id)-len);
	mqtt->mosq = mosquitto_new(id, 1, mqtt);
	if (mqtt->mosq == NULL) {
		log_str("ERROR: Cannot start MQTT: %s", strerror(errno));
		return -1;
	}

	// Set callbacks
	mosquitto_connect_callback_set(mqtt->mosq, mqtt_on_connect);
	mosquitto_disconnect_callback_set(mqtt->mosq, mqtt_on_disconnect);
	mosquitto_publish_callback_set(mqtt->mosq, mqtt_on_publish);
	mosquitto_message_callback_set(mqtt->mosq, mqtt_on_message);
	mosquitto_subscribe_callback_set(mqtt->mosq, mqtt_on_subscribe);
	mosquitto_log_callback_set(mqtt->mosq, mqtt_on_log);

	mqtt->update_func = update_func;
	mqtt->user_data = user_data;

	// Setup SSL
	if (ssl_dir != NULL) {
		port = MQTT_SSL_PORT;

		//TODO: mosquitto_tls_set();
	}
	else {
		port = MQTT_PORT;
	}

	// Parse broker specification
	char *str = strdup(mqtt_host);
	char *host = strchr(str, '@');
	char *user = NULL;
	char *password = NULL;

	if (host != NULL) {
		*(host++) = '\0';

		user = str;

		password = strchr(user, ':');
		if (password != NULL) {
			*(password++) = '\0';
		}
	}
	else {
		host = str;
	}

	char *p = strchr(host, ':');
	if (p != NULL) {
		*(p++) = '\0';
		port = atoi(p);
	}

	// Setup MQTT user and password
	if (mqtt_user != NULL) {
		user = mqtt_user;

		char *p = strchr(user, ':');
		if (p != NULL) {
			*(p++) = '\0';
			password = p;
		}
	}

	if (user != NULL) {
		mosquitto_username_pw_set(mqtt->mosq, user, password);
	}

	// Connect to broker
	ret = mosquitto_connect_async(mqtt->mosq, host, port, mqtt_keepalive);

	free(str);

	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to connect to MQTT broker: %s", mosquitto_strerror(ret));
		goto failed;
	}

	// Start MQTT thread
	ret = mosquitto_loop_start(mqtt->mosq);
	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to start MQTT: %s", mosquitto_strerror(ret));
		goto failed;
	}

	return 0;

failed:
	mosquitto_destroy(mqtt->mosq);
	mqtt->mosq = NULL;
	return -1;
}


int mqtt_publish(mqtt_t *mqtt, char *name, char *value, int retain)
{
	int mid;
	int ret;

	ret = mosquitto_publish(mqtt->mosq, &mid, name, strlen(value), value, mqtt_qos, retain);
	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to publish MQTT signal '%s': %s", name, mosquitto_strerror(ret));
		return -1;
	}

	log_debug(2, "MQTT publish: %s='%s' (mid=%d)", name, value, mid);

	return 0;
}


int mqtt_subscribe(mqtt_t *mqtt, char *name)
{
	int mid;
	int ret;

	ret = mosquitto_subscribe(mqtt->mosq, &mid, name, mqtt_qos);
	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to subscribe MQTT signal '%s': %s", name, mosquitto_strerror(ret));
		return -1;
	}

	log_debug(2, "MQTT subscribe: %s (mid=%d)", name, mid);

	return 0;
}
