AM_CFLAGS = @GCC_FLAGS@ @XLIBS_CFLAGS@

bin_PROGRAMS=xresponse

xresponse_SOURCES = xresponse.c

xresponse_LDADD = @XLIBS_LIBS@

EXTRA_DIST = debian/*

deb: dist
	-mkdir $(top_builddir)/debian-build
	cd $(top_builddir)/debian-build && tar zxf ../$(top_builddir)/$(PACKAGE)-$(VERSION).tar.gz
	cd $(top_builddir)/debian-build/$(PACKAGE)-$(VERSION) && dpkg-buildpackage -rfakeroot
	-rm -rf $(top_builddir)/debian-build/$(PACKAGE)-$(VERSION)

distclean :
	rm -f config.status config.log Makefile

