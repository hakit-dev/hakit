ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include ../../defs.mk

all:: $(OUTDIR)/sink.so

$(OUTDIR)/sink.so: $(OUTDIR)/main.o
