ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include ../../defs.mk

all:: $(OUTDIR)/timer-on.so

$(OUTDIR)/timer-on.so: $(OUTDIR)/main.o
