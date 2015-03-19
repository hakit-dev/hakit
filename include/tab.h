#ifndef __HAKIT_TAB_H__
#define __HAKIT_TAB_H__

#define HK_TAB_DECLARE(_var_, _type_) hk_tab_t _var_ = { .msize = sizeof(_type_), .buf = NULL, .nmemb = 0 }

typedef int (*hk_tab_foreach_func)(void *user_data, void *p);

typedef struct {
	int msize;
	void *buf;
	int nmemb;
} hk_tab_t;

extern void hk_tab_init(hk_tab_t *tab, int msize);
extern void hk_tab_alloc(hk_tab_t *tab, int msize, int nmemb);
extern void *hk_tab_push(hk_tab_t *tab);
extern void hk_tab_foreach(hk_tab_t *tab, hk_tab_foreach_func func, char *user_data);

#endif /* __HAKIT_TAB_H__ */
