AC_PREREQ(2.53)

AC_INIT([xresponse], [1.1.1], [eero.tamminen@nokia.com])
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

# allow to specify default pointer device
AC_ARG_ENABLE(pointer-device,
  AC_HELP_STRING([--enable-pointer-device=DEVICE],
    [Set pointer device used to emulate mouse input]),
  [AC_DEFINE_UNQUOTED(XINPUT_POINTER_DEVICE, "$enableval", [Default xinput pointer device])],
  [AC_DEFINE_UNQUOTED(XINPUT_POINTER_DEVICE, NULL, [Default xinput pointer device])],
)

# allow to specify default keyboard device
AC_ARG_ENABLE(keyboard-device,
  AC_HELP_STRING([--enable-keyboard-device=DEVICE],
    [Set keyboard device used to emulate keyboard input]),
  [AC_DEFINE_UNQUOTED(XINPUT_KEYBOARD_DEVICE, "$enableval", [Default xinput keyboard device])],
  [AC_DEFINE_UNQUOTED(XINPUT_KEYBOARD_DEVICE, NULL, [Default xinput keyboard device])],
)


AC_CONFIG_FILES([Makefile
				src/Makefile])

AC_OUTPUT

