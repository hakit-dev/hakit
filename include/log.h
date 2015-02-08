#ifndef __HAKIT_LOG_H__
#define __HAKIT_LOG_H__

extern void log_init(char *prefix);
extern void log_put(char *str, int len);

extern void log_tstamp(void);
extern void log_printf(const char *fmt, ...);
extern void log_str(const char *fmt, ...);
extern void log_hex(unsigned char *buf, int size);

extern void log_debug(int level, const char *fmt, ...);
extern void log_debug_data(unsigned char *buf, int size);

#endif /* __HAKIT_LOG_H__ */
