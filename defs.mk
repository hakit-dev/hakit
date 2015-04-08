CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib

CFLAGS  = -Wall -O2
LDFLAGS =

ifeq ($(HAKIT),)
VPATH = os:core:classes
CFLAGS  += -I. -Iinclude -Ios -Iclasses
else
CFLAGS  += -I$(HAKIT)/include
LDFLAGS += -L$(HAKIT)/out/$(ARCH) -lhakit
endif

-include $(wildcard $(OUTDIR)/*.d)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)
	$(eval  D := $(@:%.o=%.d))
	$(CC) -MM $(CFLAGS) $< -o $(D)
	@mv -f $(D) $(D).tmp
	@sed -e 's|^$*.o:|$(OUTDIR)/$*.o:|g' $(D).tmp >$(D)
	@fmt -1 $(D).tmp | grep '\.[ch]$$' | sed -e 's/^ *//' -e 's/$$/:/' >>$(D)
	@rm -f $(D).tmp

$(ARCH_LIBS):
	@[ -f $(OUTDIR) ] || mkdir -p $(OUTDIR)
	$(AR) rv $@ $^
	$(RANLIB) $@

$(ARCH_BINS):
	@[ -f $(OUTDIR) ] || mkdir -p $(OUTDIR)
	$(CC) -o $@ $^ $(LDFLAGS)

clean::
	rm -rf $(OUTDIR) *~
