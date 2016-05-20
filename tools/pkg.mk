##
## Linux distribution packages builder
##

VERSION ?= "0"

ifeq ($(ARCH),)
DEBARCH = $(shell dpkg-architecture -qDEB_HOST_ARCH)
RPMARCH = $(shell arch)
else

ifeq ($(ARCH),x86_64)
DEBARCH = amd64
else
DEBARCH = $(ARCH)
endif

ifeq ($(ARCH),all)
RPMARCH = noarch
else
RPMARCH = $(ARCH)
endif

endif

PKGRELEASE ?= $(shell date +%y%m%d)

DEBNAME = $(PKGNAME)_$(VERSION)-$(PKGRELEASE)_$(DEBARCH).deb

OUTDIR ?= out
DESTDIR ?= $(OUTDIR)/root
PKGDIR ?= $(OUTDIR)

ifdef PKGNAME
check_pkg_vars:
ifdef PKGDIR
	$(MKDIR) $(PKGDIR)
else
	@echo "Variable PKGDIR not defined"; false
endif
ifndef DESTDIR
	@echo "Variable DESTDIR not defined"; false
endif

rpm: check check_pkg_vars install
	$(RM) $(DESTDIR)/DEBIAN
	$(MKDIR) $(PKGDIR)/BUILD
	sed -e 's/@NAME@/$(PKGNAME)/' \
	    -e 's/@VERSION@/$(VERSION)/' \
	    -e 's/@RELEASE@/$(PKGRELEASE)/' \
	    spec.in > $(PKGDIR)/RPM.spec
	find $(DESTDIR) -type f | sed 's|^$(DESTDIR)||' > $(PKGDIR)/BUILD/RPM.files
	echo "%_topdir $(PWD)/$(PKGDIR)" > $(HOME)/.rpmmacros
	rpmbuild -bb $(PKGDIR)/RPM.spec --buildroot=$(PWD)/$(DESTDIR) --target $(RPMARCH)
	$(MV) $(PKGDIR)/RPMS/*/*.rpm $(PKGDIR)

deb: check check_pkg_vars install
	$(MKDIR) $(DESTDIR)/DEBIAN
	for file in preinst postinst prerm postrm; do \
		[ -f $(APP_DIR)targets/debian/$$file ] && install -m 755 $(APP_DIR)targets/debian/$$file $(DESTDIR)/DEBIAN/; done; \
	sed -e 's/@NAME@/$(PKGNAME)/' \
	    -e 's/@ARCH@/$(DEBARCH)/' \
	    -e 's/@VERSION@/$(VERSION)-$(PKGRELEASE)/' \
	    $(APP_DIR)control.in > $(DESTDIR)/DEBIAN/control
	fakeroot dpkg-deb --build $(DESTDIR) $(PKGDIR)/$(DEBNAME)

ipk: check check_pkg_vars install
	$(MKDIR) $(DESTDIR)/DEBIAN
	for file in preinst postinst prerm postrm; do \
		[ -f $(APP_DIR)targets/openwrt/$$file ] && install -m 755 $(APP_DIR)targets/openwrt/$$file $(DESTDIR)/DEBIAN/; done; \
	sed -e 's/@NAME@/$(PKGNAME)/' \
	    -e 's/@ARCH@/$(DEBARCH)/' \
	    -e 's/@VERSION@/$(VERSION)-$(PKGRELEASE)/' \
	    $(APP_DIR)control.in > $(DESTDIR)/DEBIAN/control
	fakeroot $(HAKIT_DIR)tools/opkg-build $(DESTDIR) $(PKGDIR)

endif
