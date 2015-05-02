HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := out/$(ARCH)

all: $(OUTDIR)/and.so

include $(HAKIT)/defs.mk

$(OUTDIR)/and.so: $(OUTDIR)/main.o
