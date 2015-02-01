ARCH ?= $(shell arch)
OUTDIR = $(ARCH)

LIB=libhakit.a
ARCH_LIB = $(OUTDIR)/$(LIB)
ARCH_LIBS = $(ARCH_LIB)

BIN = hakit-test
ARCH_BIN = $(OUTDIR)/$(BIN)
ARCH_BINS = $(ARCH_BIN)

SRCS = options.c log.c sys.c io.c buf.c command.c \
	iputils.c udpio.c tcpio.c comm.c uevent.c sysfs.c \
	gpio.c serial.c \
	http.c http_server.c eventq.c
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all: $(OUTDIR) $(ARCH_LIBS) $(ARCH_BIN)

include defs.mk

$(ARCH_LIB): $(OBJS)

$(ARCH_BIN): test.o $(ARCH_LIB)
