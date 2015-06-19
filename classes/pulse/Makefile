HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include $(HAKIT)/defs.mk

all:: $(OUTDIR)/pulse.so

$(OUTDIR)/pulse.so: $(OUTDIR)/main.o
