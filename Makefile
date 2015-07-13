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
OUTDIR := out/$(ARCH)

ARCH_LIB = $(OUTDIR)/libhakit.a
ARCH_LIBS = $(ARCH_LIB)

BINS = hakit
BINS += hakit-test-usb
#BINS += hakit-test-proc hakit-test-comm
ARCH_BINS = $(BINS:%=$(OUTDIR)/%)

include defs.mk

OS_SRCS = env.c logio.c sys.c io.c iputils.c netif.c udpio.c tcpio.c uevent.c sysfs.c \
	gpio.c serial.c proc.c mod_init.c \
	usb_io.c usb_device.c
CORE_SRCS = options.c log.c buf.c tab.c str_argv.c command.c hkcp.c comm.c mod.c mod_load.c prop.c \
	http.c eventq.c ws.c ws_utils.c ws_demo.c ws_events.c
SRCS = $(OS_SRCS) $(CORE_SRCS)
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all:: $(OUTDIR) lws $(ARCH_LIBS) $(ARCH_BINS) classes


#
# WebSockets
#
LWS_SRC_DIR = lws/libwebsockets/lib
CFLAGS += -I$(LWS_SRC_DIR) -I$(LWS_DIR)

.PHONY: lws
lws:
	make -C lws

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
$(ARCH_BINS): $(ARCH_LIBS)

$(OUTDIR)/hakit-test-proc: $(OUTDIR)/proc-test.o
$(OUTDIR)/hakit-test-comm: $(OUTDIR)/comm-test.o
$(OUTDIR)/hakit-test-usb: $(OUTDIR)/usb-test.o
$(OUTDIR)/hakit: $(OUTDIR)/hakit.o

clean::
	make -C classes clean
	make -C lws clean
	$(RM) os/*~ core/*~


#
# Install and packaging
#

INSTALL_BIN = $(DESTDIR)/usr/bin
INSTALL_SHARE = $(DESTDIR)/usr/share/hakit

install:: all
	$(MKDIR) $(INSTALL_BIN) $(INSTALL_SHARE)
	$(CP) $(ARCH_BINS) $(INSTALL_BIN)/
	$(CP) -a ui $(INSTALL_SHARE)/
	make -C classes DESTDIR=$(abspath $(HAKIT_DIR)$(DESTDIR)) install
