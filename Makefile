ARCH ?= $(shell arch)
OUTDIR = $(ARCH)

LIB=libhakit.a
ARCH_LIB = $(OUTDIR)/$(LIB)
ARCH_LIBS = $(ARCH_LIB)

BINS = hakit-test-proc hakit-test-comm
ARCH_BINS = $(BINS:%=$(OUTDIR)/%)

SRCS = options.c logio.c log.c sys.c io.c buf.c command.c \
	iputils.c udpio.c tcpio.c comm.c uevent.c sysfs.c \
	gpio.c serial.c proc.c \
	http.c http_server.c eventq.c
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all: $(OUTDIR) $(ARCH_LIBS) $(ARCH_BINS)

include defs.mk

$(ARCH_LIB): $(OBJS)

$(OUTDIR)/hakit-test-proc: $(OUTDIR)/proc-test.o $(ARCH_LIB)
$(OUTDIR)/hakit-test-comm: $(OUTDIR)/comm-test.o $(ARCH_LIB)
