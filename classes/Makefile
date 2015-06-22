CLASSES := sink source and \
	timer-pulse

all::
	for dir in $(CLASSES); do \
	  make -C $$dir all ;\
	done

clean::
	for dir in $(CLASSES); do \
	  make -C $$dir clean ;\
	done
