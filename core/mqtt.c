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
#include <fcntl.h>
#include <unistd.h>
#include <mqueue.h>

#include "sys.h"
#include "mosquitto.h"
#include "options.h"
#include "log.h"

#define __HAKIT_MQTT_C__
#include "mqtt.h"


/* MQTT default settings */
#define MQTT_DEFAULT_PORT 1883
#define MQTT_DEFAULT_SSL_PORT 8883
#define MQTT_DEFAULT_KEEPALIVE 60

/* Message queue */
#define MSG_MAXSIZE 1024

/* Reconnect delay */
#define MQTT_RECONNECT_DELAY 60

typedef enum {
	MQTT_ST_DISCONNECTED=0,
	MQTT_ST_CONNECTING,
	MQTT_ST_CONNECTED,
	MQTT_ST_RECONNECT
} mqtt_state_t;

struct __mqtt_s {
	char *broker;
	struct mosquitto *mosq;
	mqtt_update_func_t update_func;
	void *user_data;
	mqtt_state_t state;
	sys_tag_t timeout_tag;
	mqd_t mq;
	sys_tag_t mq_tag;
	int port;
	int qos;
};


static int mqtt_connect_now(mqtt_t *mqtt);


static void mqtt_timeout_stop(mqtt_t *mqtt)
{
	if (mqtt->timeout_tag != 0) {
		sys_remove(mqtt->timeout_tag);
		mqtt->timeout_tag = 0;
	}
}


static void mqtt_on_connect(struct mosquitto *mosq, void *obj, int rc)
{
	mqtt_t *mqtt = obj;

	log_debug(2, "MQTT connected: rc=%d", rc);
	mqtt->state = MQTT_ST_CONNECTED;

	mqtt_timeout_stop(mqtt);

	if (mqtt->update_func != NULL) {
		mqtt->update_func(mqtt->user_data, NULL, NULL);
	}
}


static void mqtt_on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
	mqtt_t *mqtt = obj;

	log_debug(2, "MQTT disconnected: rc=%d", rc);

	mqtt_timeout_stop(mqtt);

	if (mqtt->state == MQTT_ST_RECONNECT) {
		mqtt_connect_now(mqtt);
	}
	else if (mqtt->state == MQTT_ST_CONNECTED) {
		mqtt->timeout_tag = sys_timeout(MQTT_RECONNECT_DELAY*1000, (sys_func_t) mqtt_connect_now, mqtt);
	}
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
	case MOSQ_LOG_INFO:    tag = "INFO   "; break;
	case MOSQ_LOG_NOTICE:  tag = "NOTICE "; break;
	case MOSQ_LOG_WARNING: tag = "WARNING"; break;
	case MOSQ_LOG_ERR:     tag = "ERROR  "; break;
	case MOSQ_LOG_DEBUG:   tag = (opt_debug > 0) ? "DEBUG  ":NULL; break;
	default:               tag = NULL; break;
	}

	if (tag != NULL) {
		log_str("MQTT-client %s %s", tag, str);
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


static int mqtt_connect_now(mqtt_t *mqtt)
{
	log_debug(2, "mqtt_connect_now (state=%d)", mqtt->state);
	mqtt->timeout_tag  = 0;
	
	// Parse broker specification
	char *str = strdup(mqtt->broker);
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

	int port = mqtt->port;
	char *p = strchr(host, ':');
	if (p != NULL) {
		*(p++) = '\0';
		port = atoi(p);
	}

	if (user != NULL) {
		log_debug(1, "MQTT user: '%s'", user);
		mosquitto_username_pw_set(mqtt->mosq, user, password);
	}

	if (*host == '\0') {
		host = "localhost";
	}

	// Connect to broker
	log_str("MQTT connect: %s:%d", host, port);
	int ret = mosquitto_connect_async(mqtt->mosq, host, port, MQTT_DEFAULT_KEEPALIVE);

	free(str);

	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to connect to MQTT broker: %s", mosquitto_strerror(ret));

		/* Try to reconnect later */
		mqtt->timeout_tag = sys_timeout(MQTT_RECONNECT_DELAY*1000, (sys_func_t) mqtt_connect_now, mqtt);
	}

	return 0;
}


int mqtt_connect(mqtt_t *mqtt, char *broker)
{
	log_debug(2, "mqtt_connect (state=%d) %s", mqtt->state, broker);

	/* Do nothing if already connected to this broker */
	if (mqtt->broker != NULL) {
		if (strcmp(mqtt->broker, broker) == 0) {
			if (mqtt->state == MQTT_ST_CONNECTED) {
				log_debug(1, "MQTT client already connected to %s", broker);
			}
		}
		else {
			free(mqtt->broker);
			mqtt->broker = NULL;

			if (mqtt->state == MQTT_ST_CONNECTED) {
				mosquitto_disconnect(mqtt->mosq);
				mqtt->state = MQTT_ST_RECONNECT;
				log_debug(1, "MQTT client reconnecting to %s", broker);
			}
		}
	}

	if (mqtt->broker == NULL) {
		mqtt->broker = strdup(broker);
	}

	if ((mqtt->state != MQTT_ST_CONNECTED) && (mqtt->state != MQTT_ST_RECONNECT)) {
		mqtt_timeout_stop(mqtt);
		mqtt->timeout_tag = sys_timeout(100, (sys_func_t) mqtt_connect_now, mqtt);
		mqtt->state = MQTT_ST_CONNECTING;
	}

	return 0;
}


int mqtt_init(mqtt_t *mqtt, char *certs,
	      mqtt_update_func_t update_func, void *user_data)
{
	int major, minor, revision;
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
	if (certs != NULL) {
                int len = strlen(certs);
                char cafile[len+16];
                snprintf(cafile, sizeof(cafile), "%s/ca.crt", certs);
                char certfile[len+16];
                snprintf(certfile, sizeof(certfile), "%s/server.crt", certs);
                char keyfile[len+16];
                snprintf(keyfile, sizeof(keyfile), "%s/server.key", certs);

		mqtt->port = MQTT_DEFAULT_SSL_PORT;
                mosquitto_tls_set(mqtt->mosq, cafile, NULL,
                                  certfile, keyfile,
                                  NULL);
	}
	else {
		mqtt->port = MQTT_DEFAULT_PORT;
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

	/* Message queue receive callback */
	mqtt->mq_tag = sys_io_watch(mqtt->mq, (sys_io_func_t) mqtt_msg_recv, mqtt);

	/* Start MQTT thread */
	ret = mosquitto_loop_start(mqtt->mosq);
	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to start MQTT: %s", mosquitto_strerror(ret));
		goto failed;
	}

	return 0;

failed:
	mqtt_shutdown(mqtt);
	return -1;
}


void mqtt_shutdown(mqtt_t *mqtt)
{
	/* Cancel running timer */
	mqtt_timeout_stop(mqtt);

	/* Close messaging pipe endpoints */
	if (mqtt->mq_tag != 0) {
		sys_remove(mqtt->mq_tag);
		mqtt->mq_tag = 0;
	}

	if (mqtt->mq != -1) {
		mq_close(mqtt->mq);
		mqtt->mq = -1;
	}

	/* Kill Mosquitto instance */
	if (mqtt->mosq != NULL) {
		mosquitto_destroy(mqtt->mosq);
		mqtt->mosq = NULL;
	}

	if (mqtt->broker != NULL) {
		free(mqtt->broker);
		mqtt->broker = NULL;
	}
}


int mqtt_publish(mqtt_t *mqtt, char *name, char *value, int retain)
{
	int mid;
	int ret;

	if (mqtt->mosq == NULL) {
		return -1;
	}

	if (mqtt->state != MQTT_ST_CONNECTED) {
		return -1;
	}

	ret = mosquitto_publish(mqtt->mosq, &mid, name, strlen(value), value, mqtt->qos, retain);
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

	if (mqtt->state != MQTT_ST_CONNECTED) {
		return -1;
	}

	ret = mosquitto_subscribe(mqtt->mosq, &mid, name, mqtt->qos);
	if (ret != MOSQ_ERR_SUCCESS) {
		log_str("ERROR: Failed to subscribe MQTT signal '%s': %s", name, mosquitto_strerror(ret));
		return -1;
	}

	log_debug(2, "MQTT subscribe: %s (mid=%d)", name, mid);

	return 0;
}

#endif /* WITH_MQTT */
