NAME := history

ARCH ?= $(shell arch)
OUTDIR = build/$(ARCH)

HISTORY_TOOL = $(OUTDIR)/hakit-history
ARCH_BINS = $(HISTORY_TOOL)

SRCS = main.c history.c

include ../classes.mk

all:: $(HISTORY_TOOL)

$(HISTORY_TOOL): $(OUTDIR)/hakit-history.o
