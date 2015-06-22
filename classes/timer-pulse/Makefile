HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include $(HAKIT)/defs.mk

all:: $(OUTDIR)/timer-pulse.so

$(OUTDIR)/timer-pulse.so: $(OUTDIR)/main.o
