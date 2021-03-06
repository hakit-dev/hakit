#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

CHECK_PACKAGES_deb += imagemagick ruby-sass
CHECK_PACKAGES_rpm += ImageMagick

ARCH ?= $(shell arch)
OUTDIR = build/$(ARCH)

include ../defs.mk

INSTALL_SHARE = $(DESTDIR)/usr/share/hakit/ui

all:: check css

.PHONY: css
css: switch3.css

clean::
	$(RM) *~

install: all
	$(MKDIR) $(INSTALL_SHARE)
	$(CP) -a *.html *.css *.js favicon.ico vendor $(INSTALL_SHARE)/

%.css: %.scss
	sass $< $@
