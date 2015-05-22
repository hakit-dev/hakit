ARCH ?= $(shell arch)
OUTDIR = out/$(ARCH)

GIT_URL = git://git.libwebsockets.org/libwebsockets
LWS_VERSION = v1.4-chrome43-firefox-36

SRC_DIR = libwebsockets
LIB_DIR = $(OUTDIR)/lib

INC = $(SRC_DIR)/lib/libwebsockets.h
LIB = $(LIB_DIR)/libwebsockets.a

all: $(OUTDIR) $(LIB)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(INC):
	git clone $(GIT_URL)
	git -C $(SRC_DIR) checkout $(LWS_VERSION)

$(LIB): $(INC)
	(cd $(OUTDIR) && cmake ../../$(SRC_DIR))
	make -C $(OUTDIR)

clean::
	rm -rf $(OUTDIR) *~

mrproper:: clean
	rm -rf $(SRC_DIR) 
