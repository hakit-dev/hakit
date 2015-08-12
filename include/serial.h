#ifndef __HAKIT_SERIAL_H__
#define __HAKIT_SERIAL_H__

extern int serial_open(char *devname, unsigned int speed, int rtscts);
extern int serial_set_custom_speed(int fd, unsigned int speed);
extern void serial_close(int fd);

#endif // __HAKIT_SERIAL_H__
