#
# HAKit - The Home Automation KIT
# Copyright (C) 2014-2020 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

HAKIT_BUILD := 1
ARCH ?= $(shell arch)
DISTRO ?= debian

OUTDIR = ../build/$(ARCH)

ENGINE_BIN = $(OUTDIR)/hakit-engine
CLIENT_BIN = $(OUTDIR)/hakit-client
LAUNCHER_BIN = $(OUTDIR)/hakit-launcher
ARCH_BINS = $(ENGINE_BIN) $(CLIENT_BIN) $(LAUNCHER_BIN)

include ../defs.mk

CFLAGS += -I$(HAKIT_DIR)os -I$(HAKIT_DIR)core
LDFLAGS += -rdynamic -ldl

ARCH_LIB = $(OUTDIR)/libhakit.a
CLASSES_LIB = $(OUTDIR)/libhakit_classes.a

all:: $(ARCH_BINS)

$(ENGINE_BIN): $(ENGINE_BIN).o $(CLASSES_LIB) $(ARCH_LIB)
$(CLIENT_BIN): $(CLIENT_BIN).o $(ARCH_LIB)
$(LAUNCHER_BIN): $(LAUNCHER_BIN).o $(ARCH_LIB)

INSTALL_BIN = $(DESTDIR)/usr/bin

install:: all
	$(MKDIR) $(INSTALL_BIN)
	$(CP) $(ENGINE_BIN) $(LAUNCHER_BIN) $(CLIENT_BIN) $(INSTALL_BIN)/

clean::
	$(RM) $(ARCH_BINS)
