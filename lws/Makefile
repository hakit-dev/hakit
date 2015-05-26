ARCH ?= $(shell arch)
OUTDIR = out/$(ARCH)

GIT_URL = git://git.libwebsockets.org/libwebsockets
LWS_VERSION = v1.4-chrome43-firefox-36

SRC_DIR = libwebsockets
LIB_DIR = $(OUTDIR)/lib

INC = $(SRC_DIR)/lib/libwebsockets.h
LIB = $(LIB_DIR)/libwebsockets.a

ifeq ($(ARCH),mips)
CMAKE_FLAGS = -DCMAKE_TOOLCHAIN_FILE=../../cross-mips-openwrt.cmake
CMAKE_FLAGS += -DLWS_WITHOUT_EXTENSIONS=1 -DLWS_WITH_ZLIB=0
#CMAKE_FLAGS += -DOPENSSL_ROOT_DIR="$(STAGING_DIR)/target-mips_r2_uClibc-0.9.33.2/usr"
endif
CMAKE_FLAGS += -DLWS_WITH_SSL=0

all: $(OUTDIR) $(LIB)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(INC):
	git clone $(GIT_URL)
	git -C $(SRC_DIR) checkout $(LWS_VERSION)

$(LIB): $(INC)
	(cd $(OUTDIR) && cmake ../../$(SRC_DIR) $(CMAKE_FLAGS))
	make -C $(OUTDIR)

clean::
	rm -rf $(OUTDIR) *~

mrproper:: clean
	rm -rf $(SRC_DIR) 
