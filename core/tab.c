#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "tab.h"


void hk_tab_init(hk_tab_t *tab, int msize)
{
	tab->buf = NULL;
	tab->nmemb = 0;
	tab->msize = msize;
}


void hk_tab_alloc(hk_tab_t *tab, int msize, int nmemb)
{
	int size = nmemb * msize;
	tab->buf = malloc(size);
	tab->nmemb = nmemb;
	tab->msize = msize;
	memset(tab->buf, 0, size);
}


void *hk_tab_push(hk_tab_t *tab)
{
	void *p;
	int i = tab->nmemb;
	tab->nmemb++;
	tab->buf = realloc(tab->buf, tab->msize * tab->nmemb);

	p = tab->buf + (tab->msize * i);
	memset(p, 0, tab->msize);

	return p;
}


void hk_tab_foreach(hk_tab_t *tab, hk_tab_foreach_func func, char *user_data)
{
	int i;

	for (i = 0; i < tab->nmemb; i++) {
		void *p = tab->buf + (tab->msize * i);
		if (!func(user_data, p)) {
			return;
		}
	}
}
