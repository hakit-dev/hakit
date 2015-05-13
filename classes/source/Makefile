HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

all: $(OUTDIR)/source.so

include $(HAKIT)/defs.mk

$(OUTDIR)/source.so: $(OUTDIR)/main.o
