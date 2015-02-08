#ifndef __HAKIT_UEVENT_H__
#define __HAKIT_UEVENT_H__

typedef struct {
	char buf[2048+512];
	ssize_t size;
} uevent_data_t;

typedef void (*uevent_func_t)(void *user_data, uevent_data_t *d);

typedef struct {
	uevent_func_t func;
	void *user_data;
	char *action;
	char *subsystem;
} uevent_hook_t;

extern int uevent_init(void);
extern int uevent_add(char *action, char *subsystem, uevent_func_t func, void *user_data);
extern char *uevent_getenv(uevent_data_t *d, char *env);

#endif /* __HAKIT_UEVENT_H__ */
