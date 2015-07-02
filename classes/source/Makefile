ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include ../../defs.mk

all:: $(OUTDIR)/source.so

$(OUTDIR)/source.so: $(OUTDIR)/main.o
