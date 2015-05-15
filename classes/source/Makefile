HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include $(HAKIT)/defs.mk

all:: $(OUTDIR)/source.so

$(OUTDIR)/source.so: $(OUTDIR)/main.o
