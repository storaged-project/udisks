AC_INIT([udisks],[1.91.0],[http://bugs.freedesktop.org/enter_bug.cgi?product=udisks],[udisks])

AM_INIT_AUTOMAKE([1.9 foreign dist-bzip2 no-dist-gzip])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])

AM_MAINTAINER_MODE

AC_PROG_CC
AC_ISC_POSIX
AC_HEADER_STDC
AC_PROG_LIBTOOL

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Initialization
#

GNOME_COMMON_INIT
GNOME_DEBUG_CHECK
GNOME_COMPILE_WARNINGS([maximum])
GNOME_MAINTAINER_MODE_DEFINES

AC_PATH_PROG([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc is needed])
fi

GTK_DOC_CHECK([1.3])

GOBJECT_INTROSPECTION_CHECK([0.6.2])

# Libraries
#

PKG_CHECK_MODULES(GUDEV, [gudev-1.0 >= 147])
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.31.13])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GIO, [gio-unix-2.0 >= 2.31.13])
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(POLKIT_GOBJECT_1, [polkit-gobject-1 >= 0.92])
AC_SUBST(POLKIT_GOBJECT_1_CFLAGS)
AC_SUBST(POLKIT_GOBJECT_1_LIBS)

PKG_CHECK_MODULES(POLKIT_AGENT_1, [polkit-agent-1 >= 0.92])
AC_SUBST(POLKIT_AGENT_1_CFLAGS)
AC_SUBST(POLKIT_AGENT_1_LIBS)

PKG_CHECK_MODULES(LIBATASMART, [libatasmart >= 0.17])
AC_SUBST(LIBATASMART_CFLAGS)
AC_SUBST(LIBATASMART_LIBS)

# systemd
AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
            [],
            [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != "xno"; then
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$systemdsystemunitdir"])

# Internationalization
#

IT_PROG_INTLTOOL([$INTLTOOL_REQUIRED])
GETTEXT_PACKAGE=udisks2
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

# slash dir stuff
#

if test "$prefix" = "/usr" -o "$prefix" = "/usr/local" -o "$prefix" = "NONE" ; then
  privlibdir=/lib/udisks2
  slashlibdir=/lib
  slashsbindir=/sbin
else
  privlibdir=$prefix/udisks2
  slashlibdir=$prefix/lib
  slashsbindir=$prefix/sbin
fi
AC_SUBST(privlibdir)
AC_SUBST(slashlibdir)
AC_SUBST(slashsbindir)

# Generate
#

AC_OUTPUT([
Makefile
data/Makefile
udisks/Makefile
udisks/udisks2.pc
src/Makefile
src/tests/Makefile
tools/Makefile
doc/Makefile
doc/version.xml
doc/man/Makefile
po/Makefile.in
])

dnl ==========================================================================
echo "
               udisks $VERSION
             ================

        prefix:                     ${prefix}
        libdir:                     ${libdir}
        libexecdir:                 ${libexecdir}
        bindir:                     ${bindir}
        sbindir:                    ${sbindir}
        datadir:                    ${datadir}
        sysconfdir:                 ${sysconfdir}
        localstatedir:              ${localstatedir}
        docdir:                     ${docdir}
        privlibdir:                 ${privlibdir}
        slashlibdir:                ${slashlibdir}
        slashsbindir:               ${slashsbindir}
        introspection:              ${found_introspection}
        systemdsystemunitdir:       ${systemdsystemunitdir}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}

        Maintainer mode:            ${USE_MAINTAINER_MODE}
        Building api docs:          ${enable_gtk_doc}
"
