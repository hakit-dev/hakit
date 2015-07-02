HAKIT_DIR := $(dir $(lastword $(MAKEFILE_LIST)))../

ARCH ?= $(shell arch)
OUTDIR := device/$(ARCH)

include $(HAKIT_DIR)defs.mk

BIN = $(OUTDIR)/$(NAME).so
OBJS ?= main.o

all:: $(BIN)

$(BIN): $(OBJS:%=$(OUTDIR)/%)
