CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
RM = rm -rf

CFLAGS  = -Wall -O2 -fPIC
LDFLAGS =

ifeq ($(HAKIT),)
VPATH = os:core:classes
CFLAGS  += -I. -Iinclude -Ios
LDFLAGS += -L$(OUTDIR) -lhakit
else
CFLAGS  += -I$(HAKIT)/include
LDFLAGS += -L$(HAKIT)/out/$(ARCH) -lhakit
endif

-include $(wildcard $(OUTDIR)/*.d)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(OUTDIR)/%.o: %.c
	@[ -f $(OUTDIR) ] || mkdir -p $(OUTDIR)
	$(CC) -o $@ -c $< $(CFLAGS)
	$(eval  D := $(@:%.o=%.d))
	$(CC) -MM $(CFLAGS) $< -o $(D)
	@mv -f $(D) $(D).tmp
	@sed -e 's|^$*.o:|$(OUTDIR)/$*.o:|g' $(D).tmp >$(D)
	@fmt -1 $(D).tmp | grep '\.[ch]$$' | sed -e 's/^ *//' -e 's/$$/:/' >>$(D)
	@$(RM) $(D).tmp

$(OUTDIR)/%.so:
	@[ -f $(OUTDIR) ] || mkdir -p $(OUTDIR)
	$(CC) -o $@ $^ $(LDFLAGS) -shared -nostartfiles

$(ARCH_LIBS):
	@[ -f $(OUTDIR) ] || mkdir -p $(OUTDIR)
	$(AR) rv $@ $^
	$(RANLIB) $@

$(ARCH_BINS):
	@[ -f $(OUTDIR) ] || mkdir -p $(OUTDIR)
	$(CC) -o $@ $^ $(LDFLAGS)

clean::
	$(RM) $(OUTDIR) *~
