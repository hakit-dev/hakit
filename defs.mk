#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

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

#
# Tools
#

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
OBJCOPY = $(CROSS_COMPILE)objcopy
MKDIR = mkdir -p
RM = rm -rf

#
# Output directory
#

.PHONY: out-dir

all:: out-dir
out-dir: $(OUTDIR)
$(OUTDIR):
	$(MKDIR) $@

#
# Version id
#

.PHONY: version-h

VERSION := $(shell git describe --long --always --dirty 2>/dev/null)
SHORT_VERSION := $(shell echo $(VERSION) | sed -e 's/-dirty$$//' -e 's/-[a-zA-Z0-9]\+$$//')

OUT_INC := $(dir $(OUTDIR))include
VERSION_FILE := $(OUT_INC)/.version
PREV_VERSION := $(shell cat $(VERSION_FILE) 2>/dev/null)
ifneq ($(VERSION),$(PREV_VERSION))
all:: version-h
endif

ifneq ($(HAKIT),)
CFLAGS  += -I$(HAKIT)/out/include
endif
CFLAGS  += -I$(OUT_INC)
CFLAGS  += -DARCH="$(ARCH)"

version-h: out-dir
	$(MKDIR) $(OUT_INC)
	echo $(VERSION) >$(VERSION_FILE)
ifeq ($(HAKIT),)
	echo '#define HAKIT_VERSION "$(VERSION)"' >$(OUT_INC)/hakit_version.h
else
	echo '#define VERSION "$(VERSION)"' >$(OUT_INC)/version.h
endif


#
# Standard rules
#

-include $(wildcard $(OUTDIR)/*.d)

$(OUTDIR)/%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)
	$(eval  D := $(@:%.o=%.d))
	$(CC) -MM $(CFLAGS) $< -o $(D)
	@mv -f $(D) $(D).tmp
	@sed -e 's|^$*.o:|$(OUTDIR)/$*.o:|g' $(D).tmp >$(D)
	@fmt -1 $(D).tmp | grep '\.[ch]$$' | sed -e 's/^ *//' -e 's/$$/:/' >>$(D)
	@$(RM) $(D).tmp

$(OUTDIR)/%.so:
	$(CC) -o $@ $^ $(LDFLAGS) -shared -nostartfiles

$(ARCH_LIBS):
	$(AR) rv $@ $^
	$(RANLIB) $@

$(ARCH_BINS):
	$(CC) -o $@ $^ $(LDFLAGS)

clean::
	$(RM) $(OUTDIR) $(OUT_INC) *~
