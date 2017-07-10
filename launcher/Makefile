
ARCH ?= $(shell arch)
DISTRO ?= debian

OUTDIR = ../out/$(ARCH)

BINS = hakit-launcher
ARCH_BINS = $(BINS:%=$(OUTDIR)/%)

HAKIT := ..
include $(HAKIT)/defs.mk

SRCS = $(wildcard *.c)
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

all:: $(OUTDIR) $(ARCH_BINS)

$(OUTDIR)/hakit-launcher: $(OBJS)

INSTALL_BIN = $(DESTDIR)/usr/bin

install:: all
	$(MKDIR) $(INSTALL_BIN)
	$(CP) $(ARCH_BINS) $(INSTALL_BIN)/
