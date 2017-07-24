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

ARCH_LIB = $(OUTDIR)/libhakit.a
ARCH_LIBS = $(ARCH_LIB)

BINS = hakit
ifdef BUILD_TEST
BINS += hakit-test-proc hakit-test-comm hakit-test-usb
endif

ARCH_BINS = $(BINS:%=$(OUTDIR)/%)

include defs.mk

SUBDIRS := classes ui launcher

OS_SRCS = env.c logio.c sys.c io.c iputils.c netif.c netif_watch.c udpio.c tcpio.c uevent.c sysfs.c \
	gpio.c serial.c proc.c mod_init.c \
	usb_io.c usb_device.c
CORE_SRCS = options.c log.c buf.c tab.c str_argv.c command.c hkcp.c comm.c mod.c mod_load.c prop.c \
	mime.c eventq.c ws.c ws_utils.c ws_auth.c ws_events.c ws_client.c
SRCS = $(OS_SRCS) $(CORE_SRCS)
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all:: $(OUTDIR) lws $(ARCH_LIBS) $(ARCH_BINS)
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
# SSL certificate
#
.PHONY: ssl
ssl:
	make -C ssl

#
# WebSockets
#
LWS_SRC_DIR = lws/libwebsockets/lib
CFLAGS += -I$(LWS_SRC_DIR) -I$(LWS_DIR)

.PHONY: lws
lws:
	make -C lws TARGET=$(TARGET)

#
# HAKit libs and bins
#
LDFLAGS += -rdynamic -ldl

$(ARCH_LIB): $(OBJS)

$(OUTDIR)/hakit-test-proc: $(OUTDIR)/proc-test.o $(ARCH_LIBS)
$(OUTDIR)/hakit-test-comm: $(OUTDIR)/comm-test.o $(ARCH_LIBS)
$(OUTDIR)/hakit-test-usb: $(OUTDIR)/usb-test.o $(ARCH_LIBS)
$(OUTDIR)/hakit: $(OUTDIR)/hakit.o $(OBJS)

clean::
	for dir in $(SUBDIRS); do \
	  make -C "$$dir" clean ;\
	done
	make -C lws clean
	$(RM) os/*~ core/*~


#
# Install and packaging
#

INSTALL_DESTDIR = $(abspath $(HAKIT_DIR)$(DESTDIR))

INSTALL_BIN = $(DESTDIR)/usr/bin
INSTALL_SHARE = $(DESTDIR)/usr/share/hakit
INSTALL_INIT = $(DESTDIR)/etc/init.d
INSTALL_SYSTEMD = $(DESTDIR)/etc/systemd/system

install:: all
	$(MKDIR) $(INSTALL_BIN) $(INSTALL_SHARE) $(INSTALL_INIT) $(INSTALL_SYSTEMD)
	$(CP) $(ARCH_BINS) $(INSTALL_BIN)/
	$(CP) -a test/timer.hk $(INSTALL_SHARE)/test.hk
	$(CP) -a targets/$(DISTRO)/hakit.sh $(INSTALL_INIT)/hakit
	for dir in $(SUBDIRS); do \
	  make -C "$$dir" DESTDIR=$(INSTALL_DESTDIR) install ;\
	done
ifneq ($(WITHOUT_SSL),yes)
	make -C ssl DESTDIR=$(INSTALL_DESTDIR) install
endif

ifneq ($(wildcard targets/$(DISTRO)/hakit.service),)
install::
	$(MKDIR) $(INSTALL_SYSTEMD)
	$(CP) -a targets/$(DISTRO)/hakit.service $(INSTALL_SYSTEMD)/
endif
