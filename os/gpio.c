#include <stdio.h>
#include <unistd.h>

#include "log.h"
#include "sysfs.h"
#include "gpio.h"


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
	char path[64];
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


void gpio_set_output(int n)
{
	gpio_set_entry_str(n, "direction", "out");
}


void gpio_set_value(int n, int value)
{
	gpio_set_entry_int(n, "value", value);
}


int gpio_get_value(int n)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", n);
	return sysfs_read_int(path);
}
