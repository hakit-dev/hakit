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

OUTDIR = ../build/$(ARCH)

ARCH_LIB = $(OUTDIR)/libhakit.a
ARCH_LIBS = $(ARCH_LIB)

include ../defs.mk

CFLAGS += -I$(HAKIT_DIR)os
LDFLAGS += -rdynamic -ldl

LIB_SRCS = options.c log.c buf.c tab.c str_argv.c command.c endpoint.c mod.c mod_load.c prop.c \
	advertise.c hkcp.c hkcp_cmd.c mqtt.c comm.c trace.c \
	mime.c eventq.c ws.c ws_utils.c ws_auth.c ws_events.c ws_client.c
LIB_OBJS = $(LIB_SRCS:%.c=$(OUTDIR)/%.o)

$(ARCH_LIB): $(LIB_OBJS)

all:: $(ARCH_LIBS)

install::
	@true
