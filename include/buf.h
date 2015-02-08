#ifndef __HAKIT_BUF_H__
#define __HAKIT_BUF_H__

typedef struct {
	unsigned char *base;
	int size;
	int len;
} buf_t;

#define BUF_PTR(buf) ((buf).base + (buf).len)

extern void buf_init(buf_t *buf);
extern void buf_cleanup(buf_t *buf);
extern int buf_grow(buf_t *buf, int needed_size);

extern int buf_append(buf_t *buf, unsigned char *ptr, int len);
extern int buf_append_byte(buf_t *buf, unsigned char c);
extern int buf_append_str(buf_t *buf, char *str);
extern int buf_append_int(buf_t *buf, int i);
extern int buf_append_fmt(buf_t *buf, char *fmt, ...);

extern int buf_set(buf_t *buf, unsigned char *ptr, int len);
extern int buf_set_str(buf_t *buf, char *str);
extern int buf_set_int(buf_t *buf, int v);

extern int buf_file_load(buf_t *buf, char *filename, int silent);

#endif /* __HAKIT_BUF_H__ */
