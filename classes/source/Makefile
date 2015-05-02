HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := out/$(ARCH)

all: $(OUTDIR)/source.so

include $(HAKIT)/defs.mk

$(OUTDIR)/source.so: $(OUTDIR)/main.o
