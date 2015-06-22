HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include $(HAKIT)/defs.mk

all:: $(OUTDIR)/timer-off.so

$(OUTDIR)/timer-off.so: $(OUTDIR)/main.o
