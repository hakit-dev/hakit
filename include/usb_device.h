/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_USB_DEVICE_H__
#define __HAKIT_USB_DEVICE_H__

extern char *usb_device_find(unsigned int vendor_id, unsigned int product_id, char *serial_number);

typedef int (*usb_device_func)(unsigned int vendor_id, unsigned int product_id, char *devname, void *user_data);

extern int usb_device_list(unsigned int vendor_id, unsigned int product_id,
			   usb_device_func func, void *user_data);

#endif /* __HAKIT_USB_DEVICE_H__ */
