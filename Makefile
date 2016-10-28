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

OS_SRCS = env.c logio.c sys.c io.c iputils.c netif.c udpio.c tcpio.c uevent.c sysfs.c \
	gpio.c serial.c proc.c mod_init.c \
	usb_io.c usb_device.c
CORE_SRCS = options.c log.c buf.c tab.c str_argv.c command.c hkcp.c mqtt.c comm.c mod.c mod_load.c prop.c \
	mime.c eventq.c ws.c ws_utils.c ws_events.c ws_client.c
SRCS = $(OS_SRCS) $(CORE_SRCS)
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all:: submodules $(OUTDIR) lws $(ARCH_LIBS) $(ARCH_BINS) classes

#
# GIT submodules
#
.PHONY: submodules
submodules:
	git submodule update --init

ifneq ($(WITHOUT_SSL),yes)
all:: ssl
endif

ifneq ($(WITHOUT_MQTT),yes)
all:: mqtt
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
# MQTT
#
CFLAGS += -I$(MQTT_DIR)

.PHONY: mqtt
mqtt:
	make -C mqtt TARGET=$(TARGET)

#
# HAKit standard classes
#
.PHONY: classes
classes:
	make -C classes

LDFLAGS += -rdynamic -ldl
#LDFLAGS += -lefence

#
# HAKit libs and bins
#
$(ARCH_LIB): $(OBJS)

$(OUTDIR)/hakit-test-proc: $(OUTDIR)/proc-test.o $(ARCH_LIBS)
$(OUTDIR)/hakit-test-comm: $(OUTDIR)/comm-test.o $(ARCH_LIBS)
$(OUTDIR)/hakit-test-usb: $(OUTDIR)/usb-test.o $(ARCH_LIBS)
$(OUTDIR)/hakit: $(OUTDIR)/hakit.o $(OBJS)

clean::
	make -C classes clean
	make -C lws clean
	$(RM) os/*~ core/*~ ui/favicon.ico


#
# Install and packaging
#

INSTALL_DESTDIR = $(abspath $(HAKIT_DIR)$(DESTDIR))

INSTALL_BIN = $(DESTDIR)/usr/bin
INSTALL_SHARE = $(DESTDIR)/usr/share/hakit
INSTALL_INIT = $(DESTDIR)/etc/init.d

ifeq ($(ARCH),mips)
INIT_SCRIPT = hakit-openwrt.sh
else
INIT_SCRIPT = hakit.sh
endif

install:: all
	$(MKDIR) $(INSTALL_BIN) $(INSTALL_SHARE) $(INSTALL_INIT)
	$(CP) $(ARCH_BINS) $(INSTALL_BIN)/
	$(CP) -a test/timer.hk $(INSTALL_SHARE)/test.hk
	$(CP) -a targets/$(DISTRO)/hakit.sh $(INSTALL_INIT)/hakit
	make -C classes DESTDIR=$(INSTALL_DESTDIR) install
	make -C ui DESTDIR=$(INSTALL_DESTDIR) install
ifneq ($(WITHOUT_SSL),yes)
	make -C ssl DESTDIR=$(INSTALL_DESTDIR) install
endif
