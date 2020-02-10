HAKIT_BUILD := 1
ARCH ?= $(shell arch)

OUTDIR = ../out/$(ARCH)

ARCH_LIB = $(OUTDIR)/libhakit.a
ARCH_LIBS = $(ARCH_LIB)

include ../defs.mk

SRCS = env.c files.c gpio.c io.c iputils.c logio.c mod_init.c netif.c netif_watch.c \
	proc.c serial.c sys.c sysfs.c tcpio.c tstamp.c udpio.c uevent.c \
	usb_device.c usb_io.c
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

$(ARCH_LIB): $(OBJS)

all:: $(ARCH_LIBS)

install::
	@true
