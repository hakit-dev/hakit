ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include ../../defs.mk

all:: $(OUTDIR)/timer-off.so

$(OUTDIR)/timer-off.so: $(OUTDIR)/main.o
