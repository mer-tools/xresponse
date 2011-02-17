man1_MANS = doc/*.1
	    
SUBDIRS = src

EXTRA_DIST = debian/*

deb: dist
	-mkdir $(top_builddir)/debian-build
	cd $(top_builddir)/debian-build && tar zxf ../$(top_builddir)/$(PACKAGE)-$(VERSION).tar.gz
	cd $(top_builddir)/debian-build/$(PACKAGE)-$(VERSION) && dpkg-buildpackage -rfakeroot
	-rm -rf $(top_builddir)/debian-build/$(PACKAGE)-$(VERSION)

distclean :
	rm -f config.status config.log Makefile Makefile.in aclocal.m4 config.h config.h.in configure \
		depcomp missing stamp-h1 timestamp compile install-sh src/Makefile src/Makefile.in
