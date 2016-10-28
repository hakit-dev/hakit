CLASSES := sink source and or not cmp history \
	timer-pulse timer-on timer-off timer-clock \
	proc \
	gpio link-serial

all::
	for dir in $(CLASSES); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir all ;\
	done

clean::
	for dir in $(CLASSES); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir clean ;\
	done

install::
	for dir in $(CLASSES); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir DESTDIR=$(DESTDIR) install ;\
	done
