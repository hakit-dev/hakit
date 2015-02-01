#ifndef __SYSFS_H__
#define __SYSFS_H__

extern int sysfs_write_str(char *path, char *str);
extern char sysfs_write_int(char *path, int v);

extern int sysfs_read_str(char *path, char *buf, int size);
extern int sysfs_read_int(char *path);

#endif /* __SYSFS_H__ */
