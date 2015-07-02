NAME := or

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include ../../defs.mk

OBJ = $(OUTDIR)/$(NAME).so

all:: $(OBJ)

$(OBJ): $(OUTDIR)/main.o
