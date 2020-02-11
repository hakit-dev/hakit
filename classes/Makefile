CLASSES := $(subst /,,$(dir $(wildcard */main.c)))

all::
	for dir in $(CLASSES); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir all || exit 1;\
	done

clean::
	for dir in $(CLASSES); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir clean ;\
	done

install::
	for dir in $(CLASSES); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir DESTDIR=$(DESTDIR) install || exit 1 ;\
	done
