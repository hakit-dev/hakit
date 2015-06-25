NAME := or
HAKIT := ../..

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include $(HAKIT)/defs.mk

OBJ = $(OUTDIR)/$(NAME).so

all:: $(OBJ)

$(OBJ): $(OUTDIR)/main.o
