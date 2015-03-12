ARCH ?= $(shell arch)
OUTDIR = out/$(ARCH)

LIB=libhakit.a
ARCH_LIB = $(OUTDIR)/$(LIB)
ARCH_LIBS = $(ARCH_LIB)

BINS = hakit-test-proc hakit-test-comm hakit-adm
ARCH_BINS = $(BINS:%=$(OUTDIR)/%)

OS_SRCS = logio.c sys.c io.c iputils.c udpio.c tcpio.c uevent.c sysfs.c \
	gpio.c serial.c proc.c
CORE_SRCS = options.c log.c buf.c tab.c command.c comm.c mod.c \
	http.c http_server.c eventq.c
SRCS = $(OS_SRCS) $(CORE_SRCS)
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all: $(OUTDIR) $(ARCH_LIBS) $(ARCH_BINS)

include defs.mk

$(ARCH_LIB): $(OBJS)

$(OUTDIR)/hakit-test-proc: $(OUTDIR)/proc-test.o $(ARCH_LIB)
$(OUTDIR)/hakit-test-comm: $(OUTDIR)/comm-test.o $(ARCH_LIB)
$(OUTDIR)/hakit-adm: $(OUTDIR)/adm.o $(ARCH_LIB)
