/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifdef WITH_MQTT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>
#include <mqueue.h>

#include "sys.h"
#include "mosquitto.h"
#include "options.h"
#include "log.h"
#include "mqtt.h"


/* MQTT config options */
char *mqtt_user = NULL;
char *mqtt_host = NULL;
int mqtt_port = 0;
int mqtt_keepalive = MQTT_DEFAULT_KEEPALIVE;
int mqtt_qos = 0;

/* Message queue */
#define MSG_MAXSIZE 1024


static void mqtt_on_connect(struct mosquitto *mosq, void *obj, int rc)
{
	mqtt_t *mqtt = obj;

	log_debug(2, "MQTT connected: rc=%d", rc);
	mqtt->state = MQTT_ST_CONNECTED;
}


static void mqtt_on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
	mqtt_t *mqtt = obj;

	log_debug(2, "MQTT disconnected: rc=%d", rc);
	mqtt->state = MQTT_ST_DISCONNECTED;
}


static void mqtt_on_publish(struct mosquitto *mosq, void *obj, int mid)
{
	log_debug(2, "MQTT published: (mid=%d)", mid);
}


static void mqtt_on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	mqtt_t *mqtt = obj;
	int topiclen = strlen(msg->topic);
	int msize = topiclen + 1 + msg->payloadlen + 1;
	char mbuf[msize];
	char *topic;
	char *value;

	topic = &mbuf[0];
	memcpy(topic, msg->topic, topiclen+1);

	value = &topic[topiclen+1];
	memcpy(value, msg->payload, msg->payloadlen);
	value[msg->payloadlen] = '\0';

	log_debug(3, "MQTT message throw [%d]: %s='%s'", msize, topic, value);

	if (mq_send(mqtt->mq, mbuf, msize, 0) < 0) {
		log_str("PANIC: Cannot send MQTT message: %s", strerror(errno));
	}
}


static void mqtt_on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	log_debug(2, "MQTT subscribed: (mid=%d)", mid);
}


static void mqtt_on_log(struct mosquitto *mosq, void *obj, int level, const char *str)
{
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


static int mqtt_msg_recv(mqtt_t *mqtt, int fd)
{
	char mbuf[MSG_MAXSIZE];
	ssize_t msize;

	msize = mq_receive(mqtt->mq, mbuf, sizeof(mbuf), NULL);
	if (msize < 0) {
		if ((errno != EAGAIN) && (errno != EINTR)) {
			log_str("PANIC: Cannot handle MQTT message: %s", strerror(errno));
		}
		return 0;
	}

	if (msize < 2) {
		log_str("PANIC: Received MQTT message with illegal size (%d)", msize);
		return 0;
	}

	char *topic = &mbuf[0];
	int topiclen = strlen(topic);
	char *value = &mbuf[topiclen+1];
	log_debug(3, "MQTT message catch [%d]: %s='%s'", msize, topic, value);

	if (mqtt->update_func != NULL) {
		mqtt->update_func(mqtt->user_data, topic, value);
	}

	return 1;
}


int mqtt_init(mqtt_t *mqtt, char *ssl_dir, 
	      mqtt_update_func_t update_func, void *user_data)
{
	int major, minor, revision;
	int port;
	char id[128] = "hakit/";
	int ret;

	memset(mqtt, 0, sizeof(mqtt_t));
	mqtt->mq = -1;

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
		port = MQTT_DEFAULT_SSL_PORT;

		//TODO: mosquitto_tls_set();
	}
	else {
		port = MQTT_DEFAULT_PORT;
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
		log_debug(1, "MQTT user: '%s'", user);
		mosquitto_username_pw_set(mqtt->mosq, user, password);
	}

	// Connect to broker
	log_debug(1, "MQTT broker: %s:%d", host, port);
	ret = mosquitto_connect_async(mqtt->mosq, host, port, mqtt_keepalive);

	free(str);

	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to connect to MQTT broker: %s", mosquitto_strerror(ret));
		goto failed;
	}

	/* Create private messaging queue */
	snprintf(id, sizeof(id), "/hakit-%d", getpid());
	int flags = O_RDWR | O_CREAT | O_EXCL | O_NONBLOCK;
	struct mq_attr attr = {
		.mq_flags   = O_NONBLOCK,
		.mq_maxmsg  = 8,
		.mq_msgsize = MSG_MAXSIZE,
	};

	mqtt->mq = mq_open(id, flags, 0600, &attr);
	if (mqtt->mq == -1) {
		log_str("ERROR: Cannot create MQTT pipe: %s", strerror(errno));
		goto failed;
	}

	mq_unlink(id);  // Hide message queue from other processes

	// Start MQTT thread
	ret = mosquitto_loop_start(mqtt->mosq);
	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to start MQTT: %s", mosquitto_strerror(ret));
		goto failed;
	}

	/* Message queue receive callback */
	mqtt->mq_tag = sys_io_watch(mqtt->mq, (sys_io_func_t) mqtt_msg_recv, mqtt);

	return 0;

failed:
	/* Close messaging pipe endpoints */
	if (mqtt->mq != -1) {
		mq_close(mqtt->mq);
		mqtt->mq = -1;
	}

	/* Kill Mosquitto instance */
	if (mqtt->mosq != NULL) {
		mosquitto_destroy(mqtt->mosq);
		mqtt->mosq = NULL;
	}

	return -1;
}


int mqtt_publish(mqtt_t *mqtt, char *name, char *value, int retain)
{
	int mid;
	int ret;

	if (mqtt->mosq == NULL) {
		return -1;
	}

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

	if (mqtt->mosq == NULL) {
		return -1;
	}

	ret = mosquitto_subscribe(mqtt->mosq, &mid, name, mqtt_qos);
	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to subscribe MQTT signal '%s': %s", name, mosquitto_strerror(ret));
		return -1;
	}

	log_debug(2, "MQTT subscribe: %s (mid=%d)", name, mid);

	return 0;
}

#endif /* WITH_MQTT */
