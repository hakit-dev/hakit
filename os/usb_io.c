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
#include <malloc.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/ioctl.h>

#include "log.h"
#include "linux_usb.h"
#include "usb_io.h"


struct usbdevfs_urb *_usb_urb_submit_(int fd, int ep, int urbtype,
				      unsigned char *buf, int size)
{
	struct usbdevfs_urb *urb;
	int ret;

	if (fd < 0) {
		return NULL;
	}

	urb = malloc(sizeof(*urb));
	memset(urb, 0, sizeof(*urb));

	urb->type = urbtype;
	urb->endpoint = ep;
	urb->flags = 0;
	urb->buffer = buf;
	urb->buffer_length = size;
	//urb->usercontext = (void *)ep;
	urb->signr = 0;
	urb->actual_length = 0;
	urb->number_of_packets = 0;	/* don't do isochronous yet */

	ret = ioctl(fd, USBDEVFS_SUBMITURB, urb);
	if (ret < 0) {
		perror("URB submit");
		free(urb);
		return NULL;
	}

	return urb;
}


int _usb_urb_discard_(int fd, struct usbdevfs_urb *urb)
{
	void *context;
	int ret;

	if (urb == NULL) {
		return 0;
	}

	/*
	 * When the URB is unlinked, it gets moved to the completed list and
	 * then we need to reap it or else the next time we call this function,
	 * we'll get the previous completion and exit early
	 */
	ret = ioctl(fd, USBDEVFS_DISCARDURB, urb);
	if (ret >= 0) {
		ioctl(fd, USBDEVFS_REAPURB, &context);
	}

	/* Free URB buffer */
	free(urb);

	return ret;
}


int _usb_urb_reap_(int fd, struct usbdevfs_urb *urb)
{
	void *context;
	int ret;

	ret = ioctl(fd, USBDEVFS_REAPURB, &context);

	/*
	 * If there was an error, that wasn't EAGAIN (no completion), then
	 * something happened during the reaping and we should return that
	 * error now
	 */
	if (ret < 0) {
		if (errno == EAGAIN) {
			//fprintf(stderr, "-- _usb_urb_reap_: EAGAIN\n");
			ret = 0;
		}
		else if (errno == ENODEV) {
			ret = -2;
		}
		else {
			ret = -1;
			perror("URB reap");
		}

		if (errno != ENODEV) {
			_usb_urb_discard_(fd, urb);
		}
	}
	else {
		ret = urb->actual_length;
	}

	/* Free URB buffer */
	free(urb);

	return ret;
}


int _usb_urb_transfer_(int fd, int ep, int urbtype,
		       unsigned char *buf, int size, int timeout)
{
	struct usbdevfs_urb *urb;
	fd_set rd, wr;
	struct timeval t;
	int ret = 0;

	urb = _usb_urb_submit_(fd, ep, urbtype, buf, size);
	if (urb == NULL) {
		return -1;
	}

	/* Set wait i/o channels */
	FD_ZERO(&wr);
	FD_SET(fd, &wr);
	FD_ZERO(&rd);
	FD_SET(fd, &rd);

	/* Setup wait timeout */
	t.tv_sec = timeout / 1000;
	t.tv_usec = (timeout % 1000) * 1000;

	ret = select(fd+1, NULL, &wr, NULL, &t);
	if ( ret > 0 ) {
		ret = _usb_urb_reap_(fd, urb);
		//fprintf(stderr, "URB transfer: reaped %d/%d\n", ret, size);
	}
	else if ( ret == 0 ) {
		log_str("ERROR: URB transfer: timeout");
	}
	else {
		log_str("ERROR: URB transfer: %s", strerror(errno));
	}

	free(urb);

	return ret;
}


int _usb_bulk_read_(int fd, int ep,
		    unsigned char *buf, int size, int timeout)
{
	struct usbdevfs_bulktransfer bulk;
	int ret;

	bulk.ep = ep | USB_DIR_IN;
	bulk.len = size;
	bulk.timeout = timeout;
	bulk.data = buf;
	ret = ioctl(fd, USBDEVFS_BULK, &bulk);

	if (ret < 0) {
		log_str("ERROR: USB bulk read: %s", strerror(errno));
	}

	return ret;
}


int _usb_bulk_write_(int fd, int ep,
		     unsigned char *buf, int size, int timeout)
{
	struct usbdevfs_bulktransfer bulk;
	int ret;

	bulk.ep = ep;
	bulk.len = size;
	bulk.timeout = timeout;
	bulk.data = buf;
	ret = ioctl(fd, USBDEVFS_BULK, &bulk);

	if (ret < 0) {
		log_str("ERROR: USB bulk write: %s", strerror(errno));
	}

	return ret;
}


int _usb_interrupt_read_(int fd, int ep,
			 unsigned char *buf, int size, int timeout)
{
	/* Ensure the endpoint address is correct */
	ep |= USB_DIR_IN;
	return _usb_urb_transfer_(fd, ep, USBDEVFS_URB_TYPE_INTERRUPT, buf, size, timeout);
}


int _usb_control_msg_(int fd, unsigned char requesttype, unsigned char request,
		      int value, int index, unsigned char *buf, int size, int timeout)
{
	struct usbdevfs_ctrltransfer ctrl;
	int ret;

#ifdef OLD_USBDEVICE_FS
	ctrl.requesttype = requesttype;
	ctrl.request = request;
	ctrl.value = value;
	ctrl.index = index;
	ctrl.length = size;
#else
	ctrl.bRequestType = requesttype;
	ctrl.bRequest = request;
	ctrl.wValue = value;
	ctrl.wIndex = index;
	ctrl.wLength = size;
#endif
	ctrl.timeout = timeout;
	ctrl.data = buf;

	ret = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
	if (ret < 0) {
		log_str("ERROR: USB control: %s", strerror(errno));
	}

	return ret;
}


int _usb_serial_number_(int fd, char *str, int strsize)
{
	struct usb_device_descriptor desc;
	unsigned char buf[255];
	int ret;
	int len;
	int i;

	/* Get device descriptor */
	_usb_control_msg_(fd, USB_TYPE_STANDARD | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
			  (USB_DT_DEVICE << 8)+0, 0, (unsigned char *) &desc, sizeof(desc), 1000);

	/* Get serial number string */
	ret = _usb_control_msg_(fd, USB_TYPE_STANDARD | USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
				(USB_DT_STRING << 8)+desc.iSerialNumber, 0, buf, sizeof(buf), 1000);
	len = 0;
	for (i = 2; (i < ret) && (len < (strsize-1)); i += 2) {
		str[len++] = buf[i];
	}
	str[len] = '\0';

	return len;
}


int _usb_reset_ep_(int fd, int ep)
{
	int ret;

	ret = ioctl(fd, USBDEVFS_RESETEP, &ep);
	if (ret < 0) {
		log_str("ERROR: USB reset EP: %s", strerror(errno));
	}

	return ret;
}


int _usb_reset_device_(int fd)
{
	int ret;

	ret = ioctl(fd, USBDEVFS_RESET, NULL);
	if (ret < 0) {
		log_str("ERROR: USB device reset failed: %s", strerror(errno));
	}

	return ret;
}


int _usb_open_(char *devname)
{
	int interface = 0;
	int fd;

	/* Check USB device existance */
	if (devname == NULL) {
		return -1;
	}

	/* Open USB device */
	fd = open(devname, O_RDWR);
	if (fd < 0) {
		log_str("ERROR: Cannot open USB device %s: %s", devname, strerror(errno));
		return -1;
	}

	/* Claim USB interface 0 */
	if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interface) < 0) {
		log_str("ERROR: USB device %s: cannot claim interface %d: %s", devname, interface, strerror(errno));
		return -2;
	}

	return fd;
}


void _usb_close_(int fd)
{
	int interface = 0;

	ioctl(fd, USBDEVFS_RELEASEINTERFACE, &interface);
	close(fd);
}
