#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

ARCH ?= $(shell arch)
OUTDIR := out/$(ARCH)

ARCH_LIB = $(OUTDIR)/libhakit.a
ARCH_LIBS = $(ARCH_LIB)

BINS = hakit-test-proc hakit-test-comm hakit
ARCH_BINS = $(BINS:%=$(OUTDIR)/%)

include defs.mk

OS_SRCS = logio.c sys.c io.c iputils.c netif.c udpio.c tcpio.c uevent.c sysfs.c \
	gpio.c serial.c proc.c mod_init.c
CORE_SRCS = options.c log.c buf.c tab.c command.c comm.c mod.c mod_load.c prop.c \
	http.c eventq.c ws.c ws_utils.c ws_demo.c ws_events.c
SRCS = $(OS_SRCS) $(CORE_SRCS)
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all:: $(OUTDIR) lws $(ARCH_LIBS) $(ARCH_BINS) classes

#
# WebSockets
#
LWS_OUT_DIR = lws/out/$(ARCH)
LWS_LIB_DIR = $(LWS_OUT_DIR)/lib
LWS_SRC_DIR = lws/libwebsockets/lib

CFLAGS += -I$(LWS_SRC_DIR) -I$(LWS_OUT_DIR)
LDFLAGS += -L$(LWS_LIB_DIR) -lwebsockets

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

#
# HAKit libs and bins
#
$(ARCH_LIB): $(OBJS)
$(ARCH_BINS): $(ARCH_LIBS)

$(OUTDIR)/hakit-test-proc: $(OUTDIR)/proc-test.o
$(OUTDIR)/hakit-test-comm: $(OUTDIR)/comm-test.o
$(OUTDIR)/hakit: $(OUTDIR)/hakit.o

clean::
	make -C classes clean
	make -C lws clean
