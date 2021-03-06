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

-include $(HAKIT_DIR)config.mk

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

CFLAGS  = -Wall -fPIC -I$(HAKIT_DIR)utils/include -I$(HAKIT_DIR)os/include -I$(HAKIT_DIR)core/include
LDFLAGS =
SOFLAGS =

ifdef DEBUG
CFLAGS += -O0 -g
else
CFLAGS += -O2
endif

ifndef HAKIT_BUILD
LDFLAGS += -L$(HAKIT_DIR)build/$(ARCH) -lhakit
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
CFLAGS  += -I$(HAKIT_DIR)build/$(ARCH)
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
LWS_DIR = $(HAKIT_DIR)lws/build/$(ARCH)
LWS_INC_DIR = $(LWS_DIR)/include
LWS_SRC_DIR = $(HAKIT_DIR)lws/libwebsockets/lib
LWS_LIB_DIR = $(LWS_DIR)/lib
CFLAGS += -I$(LWS_INC_DIR) -I$(LWS_DIR)
LDFLAGS += -L$(LWS_LIB_DIR) -lwebsockets

#
# MQTT
#
ifneq ($(WITHOUT_MQTT),yes) 
MQTT_DIR = $(HAKIT_DIR)mqtt/mosquitto/lib
CFLAGS += -DWITH_MQTT -I$(MQTT_DIR)
LDFLAGS += -L$(MQTT_DIR) -lmosquitto -lpthread -lrt
endif

#
# OpenSSL
#
ifneq ($(WITHOUT_SSL),yes)
CFLAGS += -DWITH_SSL
LDFLAGS += -lssl -lcrypto
endif

#
# Standard cross-compile SDK path
#
ifdef CROSS_ROOT_PATH
ifneq ($(wildcard $(CROSS_ROOT_PATH)/usr),)
STD_CFLAGS += -I$(CROSS_ROOT_PATH)/usr/include
STD_LDFLAGS += -L$(CROSS_ROOT_PATH)/usr/lib
else
STD_CFLAGS += -I$(CROSS_ROOT_PATH)/include
STD_LDFLAGS += -L$(CROSS_ROOT_PATH)/lib
endif
endif

#
# Standard rules
#

-include $(wildcard $(OUTDIR)/*.d)

$(OUTDIR)/%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS) $(STD_CFLAGS)
	$(eval  D := $(@:%.o=%.d))
	$(CC) -MM $(CFLAGS) $(STD_CFLAGS) $< -o $(D)
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
	$(CC) -o $@ $^ $(LDFLAGS) $(STD_LDFLAGS)

clean::
	$(RM) *~

#
# Packaging
#
include $(HAKIT_DIR)tools/pkg.mk
