HAKIT_BUILD := 1
ARCH ?= $(shell arch)

OUTDIR = ../build/$(ARCH)

ARCH_LIB = $(OUTDIR)/libhakit_classes.a
ARCH_LIBS = $(ARCH_LIB)

include ../defs.mk

INIT_SRC = $(OUTDIR)/_classes.c
SRCS = $(INIT_SRC)
OBJS = $(SRCS:%.c=%.o)

CLASS_DIRS := $(subst /,,$(dir $(wildcard */main.c)))
CLASS_NAMES := $(subst -,_,$(CLASS_DIRS))
CLASS_OBJS := $(CLASS_DIRS:%=$(OUTDIR)/classes/%/device/_class.o)

.PHONY: classes

all:: classes $(ARCH_LIBS)

classes:
	for dir in $(CLASS_DIRS); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir STATIC=1 all || exit 1;\
	done

$(INIT_SRC):
	mkdir -p $(dir $@)
	echo '#include <stdio.h>' >$@
	echo '#include "mod.h"' >>$@
	for name in $(CLASS_NAMES); do \
	  echo "extern const hk_class_t _class_$$name;" >>$@ ;\
	done
	@echo "const hk_class_t *static_classes[] = {" >>$@
	for name in $(CLASS_NAMES); do \
	  echo "  &_class_$$name," >>$@ ;\
	done
	@echo "  NULL};" >>$@

$(ARCH_LIB): $(CLASS_OBJS) $(OBJS)

clean::
	rm -f $(ARCH_LIBS) $(OBJS) $(INIT_SRC)
	for dir in $(CLASS_DIRS); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir STATIC=1 clean ;\
	done

install::
	for dir in $(CLASS_DIRS); do \
	  [ -f $$dir/Makefile ] && MK=Makefile || MK=../classes.mk ;\
	  make -C $$dir -f $$MK NAME=$$dir DESTDIR=$(DESTDIR) STATIC=1 install || exit 1 ;\
	done
