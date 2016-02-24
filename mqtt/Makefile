#
# HAKit - The Home Automation KIT
# Copyright (C) 2016 Sylvain Giroudon
#
# Mosquitto build rules
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

HAKIT_DIR := ..

include $(HAKIT_DIR)/tools/check.mk

ifdef TARGET
include $(HAKIT_DIR)/targets/$(TARGET).mk
endif

ARCH ?= $(shell arch)
OUTDIR = out/$(ARCH)

SRC_DIR = org.eclipse.mosquitto
LIB_DIR = $(SRC_DIR)/lib

USE_CMAKE = 1

ifdef USE_CMAKE
LIB_NAME = libmosquitto.so
LIB = $(OUTDIR)/lib/$(LIB_NAME)
ifdef CROSS_COMPILE
CMAKE_FLAGS = -DCMAKE_TOOLCHAIN_FILE=../../cross-openwrt.cmake
endif
CMAKE_FLAGS += -DWITH_SRV=OFF
else
LIB_NAME = libmosquitto.a
LIB = $(OUTDIR)/$(LIB_NAME)
MAKE_FLAGS += WITH_SRV=no WITH_UUID=no WITH_DOCS=no DIRS=lib
endif

all:: $(LIB)

ifdef USE_CMAKE
$(LIB):
	mkdir -p $(OUTDIR)
	(cd $(OUTDIR) && cmake ../../$(SRC_DIR) $(CMAKE_FLAGS))
	make -C $(OUTDIR) libmosquitto
else
$(LIB_DIR)/$(LIB_NAME):
	make -C $(LIB_DIR) $(MAKE_FLAGS) $(LIB_NAME)

$(LIB): $(LIB_DIR)/$(LIB_NAME)
	mkdir -p $(OUTDIR)
	cp -fv $^ $@
endif

clean::
	make -C $(SRC_DIR) reallyclean
	rm -rf $(OUTDIR) *~

mrproper::
	rm -rf $(SRC_DIR) $(OUTDIR)
