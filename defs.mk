CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib

CFLAGS  = -Wall -O2
LDFLAGS =

ifneq ($(HAKIT),)
CFLAGS  += -Wall -O2 -I$(HAKIT)
LDFLAGS += -L$(HAKIT)/$(ARCH) -lhakit
endif

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

$(ARCH_LIBS):
	@[ -f $(OUTDIR) ] || mkdir -p $(OUTDIR)
	$(AR) rv $@ $^
	$(RANLIB) $@

$(ARCH_BINS):
	@[ -f $(OUTDIR) ] || mkdir -p $(OUTDIR)
	$(CC) -o $@ $^ $(LDFLAGS)

clean::
	rm -rf $(OUTDIR) *~
