HAKIT_BUILD := 1
ARCH ?= $(shell arch)

OUTDIR = ../build/$(ARCH)

ARCH_LIB = $(OUTDIR)/libhakit.a
ARCH_LIBS = $(ARCH_LIB)

include ../defs.mk

vpath %.c $(OS)

SRCS = $(wildcard *.c) $(notdir $(wildcard $(OS)/*.c))
OBJS = $(SRCS:%.c=$(OUTDIR)/%.o)

$(ARCH_LIB): $(OBJS)

all:: $(ARCH_LIBS)

list:
	@echo $(SRCS)

install::
	@true

clean::
	$(RM) $(OBJS)
