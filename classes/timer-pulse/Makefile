ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include ../../defs.mk

all:: $(OUTDIR)/timer-pulse.so

$(OUTDIR)/timer-pulse.so: $(OUTDIR)/main.o
