
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h> 
#include <linux/serial.h>

#include "log.h"
#include "serial.h"


static int serial_set_custom_speed(int fd, unsigned int speed)
{
	struct serial_struct serial;

	if (ioctl(fd, TIOCGSERIAL, &serial) < 0) {
		log_str("ERROR: ioctl(TIOCGSERIAL): %s", strerror(errno));
		return -1;
	}

	serial.flags = (serial.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
	serial.custom_divisor = serial.baud_base / speed;

	log_debug(1, "Serial device: baud_base=%d, custom_divisor=%d",
		  serial.baud_base, serial.custom_divisor);

	if (ioctl(fd, TIOCSSERIAL, &serial) < 0) {
		log_str("ERROR: ioctl(TIOCSSERIAL): %s", strerror(errno));
		return -1;
	}

	return (serial.baud_base / serial.custom_divisor);
}


static speed_t serial_get_tspeed(unsigned int speed)
{
	speed_t tspeed;

	switch (speed) {
	case     50: tspeed = B50; break;
	case     75: tspeed = B75; break;
	case    110: tspeed = B110; break;
	case    134: tspeed = B134; break;
	case    150: tspeed = B150; break;
	case    200: tspeed = B200; break;
	case    300: tspeed = B300; break;
	case    600: tspeed = B600; break;
	case   1200: tspeed = B1200; break;
	case   1800: tspeed = B1800; break;
	case   2400: tspeed = B2400; break;
	case   4800: tspeed = B4800; break;
	case   9600: tspeed = B9600; break;
	case  19200: tspeed = B19200; break;
	case  38400: tspeed = B38400; break;
	case  57600: tspeed = B57600; break;
	case 115200: tspeed = B115200; break;
	case 230400: tspeed = B230400; break;
	default: tspeed = B0; break;
	}

	return tspeed;
}


int serial_open(char *devname, unsigned int speed, int rtscts)
{
	struct termios termio;
	speed_t tspeed;
	int custom_speed = 0;
	int fd;

	log_debug(1, "Opening serial device '%s': %d bps, %shardware flow control", devname, speed, rtscts ? "":"no ");

	/* Open serial link device */
	if ((fd = open(devname, O_RDWR)) == -1) {
		log_str("ERROR: Cannot open serial device '%s': %s", devname, strerror(errno));
		return -1;
	}

	/* Check it is actually a serial link device */
	if (!isatty(fd)) {
		log_str("[ERROR] '%s' is not a serial device", devname);
		goto failed;
	}

	/* Get serial device settings */
	if (tcgetattr(fd, &termio)) {
		log_str("ERROR: tcgetattr(%s): %s", devname, strerror(errno));
		goto failed;
	}

	/* Set speed settings */
	tspeed = serial_get_tspeed(speed);
	if (tspeed == B0) {
		tspeed = B38400;
		custom_speed = 1;
	}

	/* Set speed and frame format (8N1) */
	/*termio.c_cflag = "Inherited from previous configuration using stty" */
	termio.c_oflag = 0;                /* Mode raw */
	termio.c_iflag = 0;
	termio.c_cflag = CS8 | CREAD | CLOCAL;
	if ( rtscts )
		termio.c_cflag |= CRTSCTS;

	termio.c_lflag = 0;                /* Non-canonical mode */
	termio.c_line = 0;                 /* Line discipline 0 */
	termio.c_cc[VMIN] = 1;
	termio.c_cc[VTIME] = 0;

	cfsetispeed(&termio, speed);
	cfsetospeed(&termio, speed);

	if ( tcsetattr(fd, TCSANOW, &termio) ) {
		log_str("ERROR: tcsetattr(%s): %s", devname, strerror(errno));
		goto failed;
	}

	/* Flush all buffers */
	tcflush(fd, TCIOFLUSH);

	if (custom_speed) {
		serial_set_custom_speed(fd, speed);
	}

	return fd;

failed:
	close(fd);
	return -1;
}
