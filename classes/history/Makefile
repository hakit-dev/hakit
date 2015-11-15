SRCS = main.c history.c
OBJS = $(SRCS:%.c=%.o)

include ../classes.mk

HISTORY_TOOL = $(OUTDIR)/hakit-history

all:: $(HISTORY_TOOL)

$(HISTORY_TOOL): $(OUTDIR)/hakit-history.o
