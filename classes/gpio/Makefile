NAME := gpio

ARCH ?= $(shell arch)
OUTDIR = build/$(ARCH)

SRCS = main.c sysfs.c gpio.c
OBJS = $(SRCS:%.c=%.o)

include ../classes.mk
