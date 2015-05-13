HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

all: $(OUTDIR)/and.so

include $(HAKIT)/defs.mk

$(OUTDIR)/and.so: $(OUTDIR)/main.o
