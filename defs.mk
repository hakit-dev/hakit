#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

HAKIT_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
APP_DIR := $(dir $(firstword $(MAKEFILE_LIST)))
ifeq ($(APP_DIR),$(HAKIT_DIR))
HAKIT_BUILD := 1
endif

#
# Check build tools dependencies
#
include $(HAKIT_DIR)tools/check.mk

#
# Compile/link options
#

CFLAGS  = -Wall -O2 -fPIC -I$(HAKIT_DIR)include
LDFLAGS =

ifdef HAKIT_BUILD
VPATH = os:core
CFLAGS  += -I. -Ios
else
LDFLAGS += -L$(HAKIT_DIR)out/$(ARCH) -lhakit
endif

#
# Tools
#

ifdef CROSS_PATH
CROSS_PREFIX = $(CROSS_PATH)/bin/$(CROSS_COMPILE)
endif

CC = $(CROSS_PREFIX)gcc
AR = $(CROSS_PREFIX)ar
RANLIB = $(CROSS_PREFIX)ranlib
OBJCOPY = $(CROSS_PREFIX)objcopy
MKDIR = mkdir -p
CP = cp -fv
RM = rm -rf
MV = mv -fv

#
# Output directory
#

.PHONY: out-dir

all:: out-dir
out-dir: $(OUTDIR)
$(OUTDIR):
	$(MKDIR) $@

#
# Version id
#

.PHONY: version-h

VERSION := $(shell $(HAKIT_DIR)tools/gitversion.sh)

ifeq ($(VERSION),)
VERSION := $(strip $(shell svnversion -c . 2>/dev/null | sed 's/^[0-9]*://'))
endif

SHORT_VERSION := $($(HAKIT_DIR)tools/gitversion.sh --short)

BUILDDATE = $(shell date +%y%m%d)

VERSION_FILE := $(OUTDIR)/.version
PREV_VERSION := $(shell cat $(VERSION_FILE) 2>/dev/null || echo 'X')
ifneq ($(VERSION),$(PREV_VERSION))
all:: version-h
endif

ifndef HAKIT_BUILD
CFLAGS  += -I$(HAKIT_DIR)out/$(ARCH)
endif
CFLAGS  += -I$(OUTDIR)

$(VERSION_FILE): out-dir
	echo $(VERSION) >$(VERSION_FILE)

version-h: $(VERSION_FILE)
ifdef HAKIT_BUILD
	echo '#define HAKIT_VERSION "$(VERSION)"' >$(OUTDIR)/hakit_version.h
	echo '#define ARCH "$(ARCH)"' >>$(OUTDIR)/hakit_version.h
else
	echo '#define VERSION "$(VERSION)"' >$(OUTDIR)/version.h
	echo '#define ARCH "$(ARCH)"' >>$(OUTDIR)/version.h
endif

#
# SDK
#
ifdef CROSS_COMPILE
SDK_DIR = $(dir $(shell readlink -f $(dir $(shell $(CC) -print-file-name=libc.a))))
ifeq ($(wildcard $(TARGET_DIR)include/linux),)
SDK_INCDIR = $(SDK_DIR)sys-include
else
SDK_INCDIR = $(SDK_DIR)include
endif
else
SDK_INCDIR = /usr/include
endif

#
# WebSockets
#
LWS_DIR = $(HAKIT_DIR)lws/out/$(ARCH)
LWS_LIB_DIR = $(LWS_DIR)/lib
LDFLAGS += -L$(LWS_LIB_DIR) -lwebsockets

#
# Standard rules
#

-include $(wildcard $(OUTDIR)/*.d)

$(OUTDIR)/%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)
	$(eval  D := $(@:%.o=%.d))
	$(CC) -MM $(CFLAGS) $< -o $(D)
	@mv -f $(D) $(D).tmp
	@sed -e 's|^$*.o:|$(OUTDIR)/$*.o:|g' $(D).tmp >$(D)
	@fmt -1 $(D).tmp | grep '\.[ch]$$' | sed -e 's/^ *//' -e 's/$$/:/' >>$(D)
	@$(RM) $(D).tmp

$(OUTDIR)/%.so:
	$(CC) -o $@ $^ -shared -nostartfiles

$(ARCH_LIBS):
	$(AR) rv $@ $^
	$(RANLIB) $@

$(ARCH_BINS):
	$(CC) -o $@ $^ $(LDFLAGS)

clean::
	$(RM) $(OUTDIR) *~

#
# Packaging
#
include $(HAKIT_DIR)tools/pkg.mk
