CLASSES := sink source and \
	timer-pulse timer-on timer-off

all::
	for dir in $(CLASSES); do \
	  make -C $$dir all ;\
	done

clean::
	for dir in $(CLASSES); do \
	  make -C $$dir clean ;\
	done
