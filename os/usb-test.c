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

#include "options.h"
#include "sys.h"
#include "log.h"
#include "usb_io.h"
#include "usb_device.h"


#define NAME "hakit-usb-test"

const char *options_summary = "HAKit USB test";

static const options_entry_t options_entries[] = {
	{ "debug",  'd', 0, OPTIONS_TYPE_INT,  &opt_debug,   "Set debug level", "N" },
	{ NULL }
};


static int usb_list(unsigned int vendor_id, unsigned int product_id, char *devname, void *user_data)
{
	char sn[80] = "";

	int fd = _usb_open_(devname);
	if (fd >= 0) {
		_usb_serial_number_(fd, sn, sizeof(sn));
		_usb_close_(fd);
	}

	log_str("  %04x:%04x %s SN='%s'", vendor_id, product_id, devname, sn);

	return 0;
}


int main(int argc, char *argv[])
{
	unsigned int vendor_id = 0;
	unsigned int product_id = 0;

	log_init(NAME);

	if (options_parse(options_entries, &argc, argv) != 0) {
		exit(1);
	}

	sys_init();

	if (argc > 1) {
		char *vstr = argv[1];
		char *pstr = strchr(vstr, ':');

		if (pstr != NULL) {
			*(pstr++) = '\0';
			if (*pstr != '\0') {
				product_id = strtoul(pstr, NULL, 16) & 0xFFFF;
			}
		}

		if (*vstr != '\0') {
			vendor_id = strtoul(vstr, NULL, 16) & 0xFFFF;
		}
	}

	log_str("List of USB devices having idVendor=%04X and idProduct=%04X:", vendor_id, product_id);
	int ret = usb_device_list(vendor_id, product_id, usb_list, NULL);
	log_str("%d USB devices found.", ret);

	return 0;
}
