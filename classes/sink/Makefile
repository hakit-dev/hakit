HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

all: $(OUTDIR)/sink.so

include $(HAKIT)/defs.mk

$(OUTDIR)/sink.so: $(OUTDIR)/main.o
