#
# HAKit - The Home Automation KIT
# Copyright (C) 2014 Sylvain Giroudon
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.
#

KEY = privkey.pem
CRT = cert.pem

INSTALL_DIR = $(DESTDIR)/usr/share/hakit/ssl

all: $(KEY) $(CRT)

$(KEY) $(CRT): openssl.cnf
	openssl req -new -newkey rsa:1024 -days 10000 -nodes -x509 -config "$<" -keyout "$(KEY)" -out "$(CRT)"

install: all
ifdef DESTDIR
	mkdir -p -m 0700 $(INSTALL_DIR)
	install -m 0600 $(KEY) $(CRT) $(INSTALL_DIR)
endif

mrproper::
	rm -f $(KEY) $(CRT)
