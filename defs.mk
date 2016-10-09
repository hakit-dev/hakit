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

-include $(HAKIT_DIR)/config.mk

ifdef TARGET
include $(HAKIT_DIR)targets/$(TARGET).mk
endif

#
# Check build tools dependencies
#
include $(HAKIT_DIR)tools/check.mk

#
# Compile/link options
#

CFLAGS  = -Wall -O0 -g -fPIC -I$(HAKIT_DIR)include
LDFLAGS =
SOFLAGS =

ifdef CROSS_ROOT_PATH
ifneq ($(wildcard $(CROSS_ROOT_PATH)/usr),)
CFLAGS += -I$(CROSS_ROOT_PATH)/usr/include
LDFLAGS += -L$(CROSS_ROOT_PATH)/usr/lib
else
CFLAGS += -I$(CROSS_ROOT_PATH)/include
LDFLAGS += -L$(CROSS_ROOT_PATH)/lib
endif
endif

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
else
CROSS_PREFIX = $(CROSS_COMPILE)
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
# WebSockets
#
LWS_DIR = $(HAKIT_DIR)lws/out/$(ARCH)
LWS_LIB_DIR = $(LWS_DIR)/lib
LDFLAGS += -L$(LWS_LIB_DIR) -lwebsockets

#
# SSL
#
ifneq ($(WITHOUT_SSL),yes)
CFLAGS += -DWITH_SSL
LDFLAGS += -lssl -lcrypto
endif

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
	$(CC) -o $@ $^ -shared -nostartfiles $(SOFLAGS)

$(ARCH_LIBS):
	$(AR) rv $@ $^
	$(RANLIB) $@

$(ARCH_BINS):
	$(CC) -o $@ $^ $(LDFLAGS)

%.ico: %.png
	convert $< -bordercolor white -border 0 \
	  \( -clone 0 -resize 16x16 \) \
	  \( -clone 0 -resize 16x16 \) \( -clone 0 -resize 32x32 \) \( -clone 0 -resize 48x48 \) \( -clone 0 -resize 64x64 \) \
	  -delete 0 -alpha off -colors 256 $@


clean::
	$(RM) $(OUTDIR) *~

#
# Packaging
#
include $(HAKIT_DIR)tools/pkg.mk
