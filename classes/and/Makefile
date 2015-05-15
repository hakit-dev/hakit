HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include $(HAKIT)/defs.mk

all:: $(OUTDIR)/and.so

$(OUTDIR)/and.so: $(OUTDIR)/main.o
