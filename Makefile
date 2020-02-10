#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

PKGNAME := hakit

ARCH ?= $(shell arch)
DISTRO ?= debian

OUTDIR = out/$(ARCH)

include defs.mk

SUBDIRS = lws
ifneq ($(WITHOUT_MQTT),yes)
SUBDIRS += mqtt
endif
SUBDIRS += os core classes ui main

all:: submodules $(OUTDIR)
	for dir in $(SUBDIRS); do \
	  make -C "$$dir" TARGET=$(TARGET) ;\
	done

ifneq ($(WITHOUT_SSL),yes)
ifndef TARGET
CHECK_PACKAGES_deb += libssl-dev
endif

all:: ssl
endif

#
# GIT submodules
#
.PHONY: submodules
submodules:
	git submodule update --init --recursive

#
# SSL certificate
#
.PHONY: ssl
ssl:
	make -C ssl

#
# WebSockets
#
CFLAGS += -I$(LWS_INC_DIR) -I$(LWS_DIR)

.PHONY: lws
lws:
	make -C lws TARGET=$(TARGET)

#
# MQTT
#
ifneq ($(WITHOUT_MQTT),yes)
CFLAGS += -I$(MQTT_DIR)
endif

.PHONY: mqtt
mqtt:
ifneq ($(WITHOUT_MQTT),yes)
	make -C mqtt TARGET=$(TARGET)
else
	@true
endif

#
# Clean
#

clean::
	$(RM) *~ $(OUTDIR)
	for dir in $(SUBDIRS); do \
	  make -C "$$dir" clean ;\
	done

#
# Install and packaging
#

INSTALL_DESTDIR = $(abspath $(HAKIT_DIR)$(DESTDIR))

INSTALL_SHARE = $(DESTDIR)/usr/share/hakit
INSTALL_INIT = $(DESTDIR)/etc/init.d
INSTALL_SYSTEMD = $(DESTDIR)/lib/systemd/system

install:: all
	$(MKDIR) $(INSTALL_SHARE)
	$(CP) -a test/timer.hk $(INSTALL_SHARE)/test.hk
	for dir in $(SUBDIRS); do \
	  make -C "$$dir" DESTDIR=$(INSTALL_DESTDIR) install ;\
	done
ifneq ($(WITHOUT_SSL),yes)
	make -C ssl DESTDIR=$(INSTALL_DESTDIR) install
endif
ifneq ($(wildcard targets/$(DISTRO)/hakit.sh),)
	$(MKDIR) $(INSTALL_INIT)
	$(CP) -a targets/$(DISTRO)/hakit.sh $(INSTALL_INIT)/hakit
endif
ifneq ($(wildcard targets/$(DISTRO)/hakit.service),)
	$(MKDIR) $(INSTALL_SYSTEMD)
	$(CP) -a targets/$(DISTRO)/hakit.service $(INSTALL_SYSTEMD)/
endif
