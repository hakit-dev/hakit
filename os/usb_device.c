/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#include "log.h"
#include "sysfs.h"
#include "usb_io.h"
#include "usb_device.h"


#define SYS_DIR "/sys/bus/usb/devices"


static char *usb_device_name(unsigned int bus, unsigned int dev)
{
	static char devname[PATH_MAX];
	snprintf(devname, sizeof(devname), "/dev/bus/usb/%03u/%03u", bus, dev);
	if (access(devname, F_OK)) {
		snprintf(devname, sizeof(devname), "/proc/bus/usb/%03u/%03u", bus, dev);
	}
	return devname;
}


typedef struct {
	char *serial_number;
	char *devname;
} usb_device_find_arg;


static int usb_device_found(unsigned int vendor_id, unsigned int product_id, char *devname, usb_device_find_arg *arg)
{
	int found = 1;

	/* Check serial number if specified */
	if (arg->serial_number != NULL) {
		int fd = _usb_open_(arg->devname);

		if (fd >= 0) {
			char sn[80];
			int len;

			len = _usb_serial_number_(fd, sn, sizeof(sn));

			if (len > 0) {
				if (strcmp(sn, arg->serial_number)) {
					found = 0;
				}
			}

			_usb_close_(fd);
		}
	}

	if (found) {
		arg->devname = devname;
	}

	return found;
}


char *usb_device_find(unsigned int vendor_id, unsigned int product_id, char *serial_number)
{
	usb_device_find_arg arg;

	arg.serial_number = serial_number;
	arg.devname = NULL;
	usb_device_list(vendor_id, product_id, (usb_device_func) usb_device_found, &arg);

	return arg.devname;
}


static int usb_device_list_proc(unsigned int vendor_id, unsigned int product_id,
				usb_device_func func, void *user_data)
{
	int count = 0;
	FILE *f;
	unsigned int c_bus = 0;
	unsigned int c_dev = 0;

	f = fopen("/proc/bus/usb/devices", "r");
	if (f == NULL) {
		return 0;
	}

	while (!feof(f)) {
		char buf[128];
		char *str, *tok;
		int len;

		str = fgets(buf, sizeof(buf), f);
		if (str == NULL)
			break;

		len = strlen(str);
		if (len < 4)
			continue;

		if (strncmp(str, "T:", 2) == 0) {
			tok = strstr(str, " Bus=");
			if (tok == NULL) {
				c_bus = c_dev = 0;
				continue;
			}

			if (sscanf(tok+5, "%u", &c_bus) != 1) {
				c_bus = c_dev = 0;
				continue;
			}

			tok = strstr(str, " Dev#=");
			if (tok == NULL) {
				c_bus = c_dev = 0;
				continue;
			}

			if (sscanf(tok+6, "%u", &c_dev) != 1) {
				c_bus = c_dev = 0;
				continue;
			}
		}
		else if (strncmp(str, "P:", 2) == 0) {
			unsigned int c_vendor_id, c_product_id;

			if ((c_bus == 0) || (c_dev == 0))
				continue;

			tok = strstr(str, " Vendor=");
			if (tok == NULL) {
				continue;
			}

			if (sscanf(tok+8, "%x", &c_vendor_id) != 1) {
				continue;
			}

			if (c_vendor_id != vendor_id) {
				continue;
			}

			tok = strstr(str, " ProdID=");
			if (tok == NULL) {
				continue;
			}

			if (sscanf(tok+8, "%x", &c_product_id) != 1) {
				continue;
			}

			if (c_product_id != product_id) {
				continue;
			}

			count++;

			if (func(c_vendor_id, c_product_id, usb_device_name(c_bus, c_dev), user_data)) {
				break;
			}
		}
	}

	fclose(f);

	return count;
}


static int usb_device_list_sys(unsigned int vendor_id, unsigned int product_id,
			       usb_device_func func, void *user_data)
{
	int count = 0;
	DIR *d;
	struct dirent *entry;

	d = opendir(SYS_DIR);
	if (d == NULL) {
		return 0;
	}

	while ((entry = readdir(d)) != NULL) {
		char path[64];
		char str[10];
		unsigned int vendor_id2;
		unsigned int product_id2;
		int busnum, devnum;

		if (entry->d_name[0] == '.') {
			continue;
		}

		/* Get vendor id */
		snprintf(path, sizeof(path), SYS_DIR "/%s/idVendor", entry->d_name);
		if (access(path, R_OK)) {
			continue;
		}
		if (sysfs_read_str(path, str, sizeof(str)) < 0) {
			continue;
		}

		vendor_id2 = strtoul(str, NULL, 16);

		/* Filter vendor id */
		if (vendor_id != 0) {
			if (vendor_id2 != vendor_id) {
				continue;
			}
		}

		/* Get product id */
		snprintf(path, sizeof(path), SYS_DIR "/%s/idProduct", entry->d_name);
		if (access(path, R_OK)) {
			continue;
		}
		if (sysfs_read_str(path, str, sizeof(str)) < 0) {
			continue;
		}

		product_id2 = strtoul(str, NULL, 16);

		/* Filter product id */
		if (product_id != 0) {
			if (product_id2 != product_id) {
				continue;
			}
		}

		/* Get bus number */
		snprintf(path, sizeof(path), SYS_DIR "/%s/busnum", entry->d_name);
		if (access(path, R_OK)) {
			continue;
		}
		busnum = sysfs_read_int(path);
		if (busnum <= 0) {
			continue;
		}

		/* Get device number */
		snprintf(path, sizeof(path), SYS_DIR "/%s/devnum", entry->d_name);
		if (access(path, R_OK)) {
			continue;
		}
		devnum = sysfs_read_int(path);
		if (devnum <= 0) {
			continue;
		}

		count++;

		if (func(vendor_id2, product_id2, usb_device_name(busnum, devnum), user_data)) {
			break;
		}
	}

	closedir(d);

	return count;
}


int usb_device_list(unsigned int vendor_id, unsigned int product_id,
		    usb_device_func func, void *user_data)
{
	int ret;

	ret = usb_device_list_sys(vendor_id, product_id, func, user_data);
	if (ret <= 0) {
		ret = usb_device_list_proc(vendor_id, product_id, func, user_data);
	}

	return ret;
}
