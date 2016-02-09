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
CORE_SRCS = options.c log.c buf.c tab.c str_argv.c command.c hkcp.c comm.c mod.c mod_load.c prop.c \
	mime.c http.c eventq.c ws.c ws_utils.c ws_events.c
SRCS = $(OS_SRCS) $(CORE_SRCS)
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all:: $(OUTDIR) lws $(ARCH_LIBS) $(ARCH_BINS) classes


#
# Linux USB API probing
#
os/usb_io.c include/usb_io.h: $(OUTDIR)/linux_usb.h
$(OUTDIR)/linux_usb.h:
	echo "// SDK include directory: $(SDK_INCDIR)" > $@
ifneq ($(wildcard $(SDK_INCDIR)/linux/usb/ch9.h),)  # kernel >= 2.6.22
	echo "#include <linux/usb/ch9.h>" >> $@
else ifneq ($(wildcard $(SDK_INCDIR)/linux/usb_ch9.h),)  # kernel >= 2.6.20
	echo "#include <linux/usb_ch9.h>" >> $@
else
	echo "#include <linux/usb.h>" >> $@
endif
ifeq ($(shell grep -q bRequestType $(SDK_INCDIR)/linux/usbdevice_fs.h && echo t),)
	echo "#define OLD_USBDEVICE_FS" >> $@
endif


#
# WebSockets
#
LWS_SRC_DIR = lws/libwebsockets/lib
CFLAGS += -I$(LWS_SRC_DIR) -I$(LWS_DIR)

.PHONY: lws
lws:
	make -C lws TARGET=$(TARGET)

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
	make -C ssl DESTDIR=$(INSTALL_DESTDIR) install
