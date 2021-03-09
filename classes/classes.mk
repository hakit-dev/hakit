HAKIT_DIR := $(dir $(lastword $(MAKEFILE_LIST)))../

ARCH ?= $(shell arch)
OUTDIR = $(HAKIT_DIR)build/$(ARCH)/classes/$(NAME)/device

include $(HAKIT_DIR)defs.mk

SRCS ?= $(wildcard *.c)
OBJS = $(SRCS:%.c=%.o)

ifdef STATIC
BIN = $(OUTDIR)/_class.o

all:: $(BIN)

$(BIN): $(OBJS:%=$(OUTDIR)/%)
	$(CROSS_PREFIX)ld -o $@ -relocatable $^

install::
	@true
else
BIN = $(OUTDIR)/$(NAME).so
INSTALL_DIR = $(DESTDIR)/usr/lib/hakit/classes/$(NAME)/device

all:: $(BIN)

$(BIN): $(OBJS:%=$(OUTDIR)/%)

install:: all
	$(MKDIR) $(INSTALL_DIR)
	$(CP) $(BIN) $(INSTALL_DIR)/
endif

clean::
	$(RM) $(OUTDIR)
