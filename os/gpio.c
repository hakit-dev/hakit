/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * GPIO access primitives
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

#include "log.h"
#include "sys.h"
#include "sysfs.h"
#include "gpio.h"


/*
 * GPIO entries
 */

typedef enum {
	GPIO_UNUSED=0,
	GPIO_INPUT,
	GPIO_OUTPUT
} gpio_dir_t;

typedef struct {
	int n;
	gpio_dir_t dir;
	int fd;
	sys_tag_t tag;
	gpio_input_func_t func;
	void *user_data;
} gpio_entry_t;


static gpio_entry_t *gpio_entries = NULL;
static int gpio_nentries = 0;


static gpio_entry_t *gpio_entry(int n)
{
	int i;

	if (n >= gpio_nentries) {
		int new_size = n + 1;
		gpio_entries = realloc(gpio_entries, sizeof(gpio_entry_t) * new_size);
		memset(&gpio_entries[gpio_nentries], 0, sizeof(gpio_entry_t) * (new_size - gpio_nentries));

		for (i = gpio_nentries; i < new_size; i++) {
			gpio_entries[i].n = i;
			gpio_entries[i].fd = -1;
		}

		gpio_nentries = new_size;
	}

	return &gpio_entries[n];
}


static int gpio_entry_set_value(gpio_entry_t *entry, int value)
{
	char buf[32];
	int len;
	int i;

	len = snprintf(buf, sizeof(buf), "%d\n", value);

	/* Write GPIO value */
	lseek(entry->fd, 0, SEEK_SET);
	if (write(entry->fd, buf, len) < 0) {
		log_str("ERROR: Failed to write GPIO entry %d: %s", entry->n, strerror(errno));
		return -1;
	}


	/* Return integer value from buffer */
	return atoi(buf);
}


static int gpio_entry_get_value(gpio_entry_t *entry)
{
	char buf[32];
	int len;
	int i;

	/* Read GPIO value */
	lseek(entry->fd, 0, SEEK_SET);
	len = read(entry->fd, buf, sizeof(buf)-1);
	if (len < 0) {
		log_str("ERROR: Failed to read GPIO entry %d: %s", entry->n, strerror(errno));
		return -1;
	}

	buf[len] = 0;

	for (i = 0; i < len; i++) {
		if (buf[i] < ' ') {
			buf[i] = 0;
			break;
		}
	}

	/* Return integer value from buffer */
	return atoi(buf);
}


static void gpio_entry_close(gpio_entry_t *entry)
{
	if (entry->tag != 0) {
		sys_remove(entry->tag);
		entry->tag = 0;
	}

	if (entry->fd >= 0) {
		close(entry->fd);
		entry->fd = -1;
	}
}


/*
 * GPIO sysfs operations
 */

static void gpio_set_entry_int(int n, char *entry, int value)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/%s", n, entry);
	sysfs_write_int(path, value);
}


static void gpio_set_entry_str(int n, char *entry, char *str)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/%s", n, entry);
	sysfs_write_str(path, str);
}


void gpio_export(int n)
{
	char path[64];

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", n);
	if (access(path, R_OK)) {
		log_debug(1, "GPIO: exporting gpio %d", n);
		sysfs_write_int("/sys/class/gpio/export", n);
	}
}


void gpio_unexport(int n)
{
	gpio_entry_t *entry = gpio_entry(n);
	char path[64];

	entry->dir = GPIO_UNUSED;
	gpio_entry_close(entry);
	entry->func = NULL;
	entry->user_data = NULL;

	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", n);
	if (access(path, R_OK) == 0) {
		log_debug(1, "GPIO: unexporting gpio %d", n);
		sysfs_write_int("/sys/class/gpio/unexport", n);
	}
}


void gpio_set_active_low(int n, int enable)
{
	gpio_set_entry_int(n, "active_low", enable);
}


int gpio_set_output(int n)
{
	gpio_entry_t *entry = gpio_entry(n);
	char path[64];

	gpio_entry_close(entry);

	/* Configure port as output */
	gpio_set_entry_str(n, "direction", "out");

	/* Open output */
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", n);
	entry->fd = open(path, O_WRONLY);
	if (entry->fd < 0) {
		log_str("ERROR: open('%s'): %s", path, strerror(errno));
		return -1;
	}

	entry->dir = GPIO_OUTPUT;

	return 0;
}


void gpio_set_value(int n, int value)
{
	gpio_entry_t *entry = gpio_entry(n);

	if (entry->dir == GPIO_OUTPUT) {
		gpio_entry_set_value(entry, value);
	}
	else {
		gpio_set_entry_int(n, "value", value);
	}
}


int gpio_get_value(int n)
{
	gpio_entry_t *entry = gpio_entry(n);
	int ret;

	if (entry->dir == GPIO_INPUT) {
		ret = gpio_entry_get_value(entry);
	}
	else {
		char path[64];
		snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", n);
		ret = sysfs_read_int(path);
	}

	return ret;
}


static int gpio_input_event(gpio_entry_t *entry, struct pollfd *pollfd)
{
	int value;

	log_debug(3, "gpio_input_event gpio=%d", entry->n);

	value = gpio_entry_get_value(entry);

	/* Invoke input callback */
	if (entry->func != NULL) {
		entry->func(entry->user_data, entry->n, value);
	}

	return (value >= 0) ? 1:0;
}


int gpio_set_input(int n, gpio_input_func_t func, void *user_data)
{
	gpio_entry_t *entry = gpio_entry(n);
	char path[64];

	gpio_entry_close(entry);

	/* Configure port as input */
	gpio_set_entry_str(n, "direction", "in");

	/* Setup input edge detection */
	gpio_set_entry_str(n, "edge", "both");

	/* Open input and hook handler */
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", n);
	entry->fd = open(path, O_RDONLY);
	if (entry->fd < 0) {
		log_str("ERROR: open('%s'): %s", path, strerror(errno));
		gpio_set_entry_str(n, "edge", "none");
		return -1;
	}

	/* Hook event handler */
	entry->tag = sys_io_poll(entry->fd, POLLPRI, (sys_poll_func_t) gpio_input_event, entry);

	entry->dir = GPIO_INPUT;
	entry->func = func;
	entry->user_data = user_data;

	return 0;
}
