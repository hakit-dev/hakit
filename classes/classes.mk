HAKIT_DIR := $(dir $(lastword $(MAKEFILE_LIST)))../

ARCH ?= $(shell arch)
OUTDIR = build/$(ARCH)

include $(HAKIT_DIR)defs.mk

BIN = $(OUTDIR)/$(NAME).so
OBJS ?= main.o

INSTALL_DIR = $(DESTDIR)/usr/lib/hakit/classes/$(NAME)/device

all:: $(BIN)

$(BIN): $(OBJS:%=$(OUTDIR)/%)

install:: all
	$(MKDIR) $(INSTALL_DIR)
	$(CP) $(BIN) $(INSTALL_DIR)/
