#ifndef __HAKIT_SERIAL_H__
#define __HAKIT_SERIAL_H__

#define SERIAL_DSR 0x01
#define SERIAL_CTS 0x02
#define SERIAL_CD  0x04
#define SERIAL_RI  0x08
#define SERIAL_DTR 0x10
#define SERIAL_RTS 0x20

extern int serial_open(char *devname, unsigned int speed, int rtscts);
extern void serial_close(int fd);

extern int serial_modem_wait(int fd);
extern int serial_modem_get(int fd);
extern int serial_modem_set(int fd, int flags, int value);

#endif // __HAKIT_SERIAL_H__
