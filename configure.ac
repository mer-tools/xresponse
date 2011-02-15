AC_PREREQ(2.53)

AC_INIT([xresponse], 0.3.2, [eero.tamminen@nokia.com])
AC_CONFIG_SRCDIR([src/xresponse.c])

AM_INIT_AUTOMAKE()

AM_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

#PKG_CHECK_MODULES(X11, x11, [have_libx11pc="yes"], [have_libx11pc="no"])

PKG_CHECK_MODULES(GLIB, [glib-2.0])

# Very lazy check, possibly do old way aswell, but damage will be needed 
# whatever so likely will need autoconfed ( fd.o ) xlibs.
PKG_CHECK_MODULES(XLIBS, x11 xext xtst xdamage xi)

if test "x$GCC" = "xyes"; then
        GCC_FLAGS="-g -Wall"
fi

AC_CONFIG_FILES([Makefile
				src/Makefile])

AC_OUTPUT

