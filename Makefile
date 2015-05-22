ARCH ?= $(shell arch)
OUTDIR = out/$(ARCH)

ifneq ($(ARCH),mips)
ENABLE_LWS = 1
endif

ARCH_LIB = $(OUTDIR)/libhakit.a
ARCH_LIBS = $(ARCH_LIB)

BINS = hakit-test-proc hakit-test-comm hakit
ARCH_BINS = $(BINS:%=$(OUTDIR)/%)

OS_SRCS = logio.c sys.c io.c iputils.c netif.c udpio.c tcpio.c uevent.c sysfs.c \
	gpio.c serial.c proc.c mod_init.c
CORE_SRCS = options.c log.c buf.c tab.c command.c comm.c mod.c prop.c \
	http.c http_server.c eventq.c ws.c
SRCS = $(OS_SRCS) $(CORE_SRCS)
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all: $(OUTDIR) lws $(ARCH_LIBS) $(ARCH_BINS) classes

include defs.mk

ifdef ENABLE_LWS
LWS_LIB_DIR = lws/out/$(ARCH)/lib
LWS_SRC_DIR = lws/libwebsockets/lib

CFLAGS += -I$(LWS_SRC_DIR) -DENABLE_LWS
LDFLAGS += -L$(LWS_LIB_DIR) -lwebsockets

.PHONY: lws
lws:
	make -C lws
else
lws:
	echo "WARNING: WebSockets not supported on arch '$(ARCH)'" 
endif

.PHONY: classes
classes:
	make -C classes


VERSION := $(shell git describe --long --always --dirty 2>/dev/null || cat $(ROOT_DIR)VERSION 2>/dev/null)
SHORT_VERSION := $(shell echo $(VERSION) | sed -e 's/-dirty$$//' -e 's/-[a-zA-Z0-9]\+$$//')
CFLAGS += -DHAKIT_VERSION="$(VERSION)" -DARCH="$(ARCH)"

LDFLAGS += -rdynamic -ldl

$(ARCH_LIB): $(OBJS)

$(OUTDIR)/hakit-test-proc: $(OUTDIR)/proc-test.o
$(OUTDIR)/hakit-test-comm: $(OUTDIR)/comm-test.o
$(OUTDIR)/hakit-adm: $(OUTDIR)/adm.o
$(OUTDIR)/hakit: $(OUTDIR)/hakit.o

clean::
	make -C classes clean
	make -C lws clean
