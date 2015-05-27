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

ARCH ?= $(shell arch)
OUTDIR = out/$(ARCH)

GIT_URL = git://git.libwebsockets.org/libwebsockets
LWS_VERSION = v1.4-chrome43-firefox-36
#LWS_VERSION = master

SRC_DIR = libwebsockets
LIB_DIR = $(OUTDIR)/lib

INC = $(SRC_DIR)/lib/libwebsockets.h
LIB = $(LIB_DIR)/libwebsockets.a

ifeq ($(ARCH),mips)
CMAKE_FLAGS = -DCMAKE_TOOLCHAIN_FILE=../../cross-mips-openwrt.cmake
#CMAKE_FLAGS += -DOPENSSL_ROOT_DIR="$(STAGING_DIR)/target-mips_r2_uClibc-0.9.33.2/usr"
endif
CMAKE_FLAGS += -DLWS_WITHOUT_EXTENSIONS=1 -DLWS_WITH_ZLIB=0
CMAKE_FLAGS += -DLWS_WITH_SSL=0
CMAKE_FLAGS += -DLWS_WITHOUT_TESTAPPS=1 -DLWS_WITHOUT_DAEMONIZE=1
CMAKE_FLAGS += -DLWS_WITH_STATIC=1

all: $(OUTDIR) $(LIB)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(INC):
	git clone $(GIT_URL)
	git -C $(SRC_DIR) checkout $(LWS_VERSION)

$(LIB): $(INC)
	(cd $(OUTDIR) && cmake ../../$(SRC_DIR) $(CMAKE_FLAGS))
	make -C $(OUTDIR)
	$(RM) $(LIB_DIR)/*.so

clean::
	rm -rf $(OUTDIR) *~

mrproper:: clean
	rm -rf $(SRC_DIR) 
