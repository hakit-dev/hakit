/*
 * HAKit - The Home Automation KIT - www.hakit.net
 * Copyright (C) 2014 Sylvain Giroudon
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __HAKIT_USB_IO_H__
#define __HAKIT_USB_IO_H__

#include <linux/usbdevice_fs.h>
#include "linux_usb.h"

extern struct usbdevfs_urb *_usb_urb_submit_(int fd, int ep, int urbtype,
					     unsigned char *buf, int size);

extern int _usb_urb_discard_(int fd, struct usbdevfs_urb *urb);

extern int _usb_urb_reap_(int fd, struct usbdevfs_urb *urb);

extern int _usb_urb_transfer_(int fd, int ep, int urbtype,
			      unsigned char *buf, int size, int timeout);

extern int _usb_bulk_read_(int fd, int ep,
			   unsigned char *buf, int size, int timeout);

extern int _usb_bulk_write_(int fd, int ep,
			    unsigned char *buf, int size, int timeout);

extern int _usb_interrupt_read_(int fd, int ep,
				unsigned char *buf, int size, int timeout);

extern int _usb_control_msg_(int fd, unsigned char requesttype, unsigned char request,
			     int value, int index, unsigned char *buf, int size, int timeout);

extern int _usb_reset_ep_(int fd, int ep);

extern int _usb_reset_device_(int fd);

extern int _usb_open_(char *devname);
extern void _usb_close_(int fd);

extern int _usb_serial_number_(int fd, char *str, int strsize);

#endif /* __HAKIT_USB_IO_H__ */
