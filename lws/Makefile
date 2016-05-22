#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# libwebsockets build rules
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

HAKIT_DIR := ..

-include $(HAKIT_DIR)/config.mk

CHECK_PACKAGES = cmake
include $(HAKIT_DIR)/tools/check.mk

ifdef TARGET
include $(HAKIT_DIR)/targets/$(TARGET).mk
endif

ARCH ?= $(shell arch)
OUTDIR = out/$(ARCH)

SRC_DIR = libwebsockets
LIB_DIR = $(OUTDIR)/lib

LIB = $(LIB_DIR)/libwebsockets.a

ifeq ($(TARGET),rpi)
CMAKE_FLAGS = -DCMAKE_TOOLCHAIN_FILE=../../cross-rpi.cmake
else ifeq ($(DISTRO),openwrt)
CMAKE_FLAGS = -DCMAKE_TOOLCHAIN_FILE=../../cross-openwrt.cmake
#CMAKE_FLAGS += -DOPENSSL_ROOT_DIR="$(STAGING_DIR)/target-mips_r2_uClibc-0.9.33.2/usr"
endif
CMAKE_FLAGS += -DLWS_WITHOUT_EXTENSIONS=ON -DLWS_WITH_ZLIB=OFF
ifeq ($(WITHOUT_SSL),yes)
CMAKE_FLAGS += -DLWS_WITH_SSL=OFF
else
CMAKE_FLAGS += -DLWS_WITH_SSL=ON
endif
CMAKE_FLAGS += -DLWS_WITHOUT_TESTAPPS=ON -DLWS_WITHOUT_DAEMONIZE=ON -DLWS_WITHOUT_CLIENT=ON
CMAKE_FLAGS += -DLWS_WITH_STATIC=ON -DLWS_WITH_SHARED=OFF

all:: $(OUTDIR) $(LIB)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(LIB):
	git submodule update --init
	(cd $(OUTDIR) && cmake ../../$(SRC_DIR) $(CMAKE_FLAGS))
	make -C $(OUTDIR)

clean::
	rm -rf $(OUTDIR) *~

mrproper:: clean
	rm -rf $(SRC_DIR) 
