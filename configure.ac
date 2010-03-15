dnl Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59c)
AC_INIT(udisks, 1.0.1, http://bugs.freedesktop.org/enter_bug.cgi?product=udisks)
AM_INIT_AUTOMAKE(udisks, 1.0.1)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC
AM_PROG_LIBTOOL
AC_PROG_MAKE_SET
AC_PROG_LN_S
AC_SYS_LARGEFILE
AM_PROG_CC_C_O

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Taken from dbus
AC_ARG_ENABLE(ansi,             [  --enable-ansi           enable -ansi -pedantic gcc flags],enable_ansi=$enableval,enable_ansi=no)
AC_ARG_ENABLE(verbose-mode,     [  --enable-verbose-mode   support verbose debug mode],enable_verbose_mode=$enableval,enable_verbose_mode=$USE_MAINTAINER_MODE)
AC_ARG_ENABLE(man-pages,        [  --enable-man-pages      build manual pages],enable_man_pages=$enableval,enable_man_pages=yes)
AC_ARG_ENABLE(profiling,        [  --enable-profiling      add tracing instrumentation for plot-timeline.py], enable_profiling=$enableval,enable_profiling=$USE_MAINTAINER_MODE)
+AC_ARG_ENABLE(strict,           [  --enable-strict         Enable strict compilation options], enable_strict=$enableval,enable_strict=$USE_MAINTAINER_MODE)

if test "x$enable_profiling" = "xyes"; then
  AC_DEFINE(PROFILING, 1, [Define to 1 if profiling is enabled ])
fi

if test "${enable_man_page}" != no; then
dnl
dnl Check for xsltproc
dnl
AC_PATH_PROG([XSLTPROC], [xsltproc])
  if test -z "$XSLTPROC"; then
    enable_man_pages=no
  fi
fi
AM_CONDITIONAL(MAN_PAGES_ENABLED, test x$enable_man_pages = xyes)

GTK_DOC_CHECK([1.3])

#### gcc warning flags

if test "x$GCC" = "xyes"; then
  changequote(,)dnl
  case " $CFLAGS " in
  *[\ \	]-Wall[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac

  case " $CFLAGS " in
  *[\ \	]-Wchar-subscripts[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wchar-subscripts" ;;
  esac

  case " $CFLAGS " in
  *[\ \	]-Wmissing-declarations[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wmissing-declarations" ;;
  esac

  case " $CFLAGS " in
  *[\ \	]-Wnested-externs[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wnested-externs" ;;
  esac

  case " $CFLAGS " in
  *[\ \	]-Wpointer-arith[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wpointer-arith" ;;
  esac

  case " $CFLAGS " in
  *[\ \	]-Wcast-align[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wcast-align" ;;
  esac

  case " $CFLAGS " in
  *[\ \	]-Wsign-compare[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wsign-compare" ;;
  esac

  case " $CFLAGS " in
  *[\ \	]-Wformat[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wformat" ;;
  esac

  case " $CFLAGS " in
  *[\ \	]-Wformat-security[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wformat-security" ;;
  esac

  if test "x$enable_ansi" = "xyes"; then
    case " $CFLAGS " in
    *[\ \	]-ansi[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -ansi" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-D_POSIX_C_SOURCE*) ;;
    *) CFLAGS="$CFLAGS -D_POSIX_C_SOURCE=199309L" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-D_BSD_SOURCE[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -D_BSD_SOURCE" ;;
    esac

    case " $CFLAGS " in
    *[\ \	]-pedantic[\ \	]*) ;;
    *) CFLAGS="$CFLAGS -pedantic" ;;
    esac
  fi

  if test "x$enable_strict" = "xyes"; then
    CFLAGS="$CFLAGS -Werror"
  fi

  changequote([,])dnl
fi

have_sgutils="false"
AC_CHECK_LIB([sgutils2], [sg_ll_inquiry], have_sgutils="true")
if test x$have_sgutils != "xtrue"; then
   AC_MSG_ERROR([libsgutils2 is needed])
fi
SGUTILS_CFLAGS=""
SGUTILS_LIBS="-lsgutils2"
AC_SUBST(SGUTILS_CFLAGS)
AC_SUBST(SGUTILS_LIBS)


PKG_CHECK_MODULES(GUDEV, [gudev-1.0 >= 147])
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.6.0])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GIO, [gio-unix-2.0 >= 2.15.0])
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(DBUS, [dbus-1 >= 1.0])
AC_SUBST(DBUS_CFLAGS)
AC_SUBST(DBUS_LIBS)

PKG_CHECK_MODULES(DBUS_GLIB, [dbus-glib-1 >= 0.82])
AC_SUBST(DBUS_GLIB_CFLAGS)
AC_SUBST(DBUS_GLIB_LIBS)

PKG_CHECK_MODULES(POLKIT_GOBJECT_1, [polkit-gobject-1 >= 0.92])
AC_SUBST(POLKIT_GOBJECT_1_CFLAGS)
AC_SUBST(POLKIT_GOBJECT_1_LIBS)

PKG_CHECK_MODULES(POLKIT_BACKEND_1, [polkit-backend-1 >= 0.92])
AC_SUBST(POLKIT_BACKEND_1_CFLAGS)
AC_SUBST(POLKIT_BACKEND_1_LIBS)

PKG_CHECK_MODULES(LIBPARTED, [libparted >= 1.8.8])
AC_SUBST(LIBPARTED_CFLAGS)
AC_SUBST(LIBPARTED_LIBS)

PKG_CHECK_MODULES(DEVMAPPER, [devmapper >= 1.02])
AC_SUBST(DEVMAPPER_CFLAGS)
AC_SUBST(DEVMAPPER_LIBS)

have_lvm2=no
AC_ARG_ENABLE(lvm2, AS_HELP_STRING([--disable-lvm2], [disable LVM2 support]))
if test "x$enable_lvm2" != "xno"; then
  PKG_CHECK_MODULES(LVM2, lvm2app >= 2.1,
                    [AC_DEFINE(HAVE_LVM2, 1, [Define if LVM2 is available]) have_lvm2=yes],
                    have_lvm2=no)
  AC_SUBST(LVM2_CFLAGS)
  AC_SUBST(LVM2_LIBS)
  if test "x$have_lvm2" = xno -a "x$enable_lvm2" = xyes; then
    AC_MSG_ERROR([lvm2 support requested but libraries not found])
  fi
fi
AM_CONDITIONAL(HAVE_LVM2, [test "$have_lvm2" = "yes"])

have_dmmp=no
AC_ARG_ENABLE(dmmp, AS_HELP_STRING([--disable-dmmp], [disable dm-multipath support]))
if test "x$enable_dmmp" != "xno"; then
  have_dmmp=yes
  AC_DEFINE(HAVE_DMMP, 1, [Define to 1 if dmmp is available])
fi
AM_CONDITIONAL(HAVE_DMMP, [test "$have_dmmp" = "yes"])

PKG_CHECK_MODULES(LIBATASMART, [libatasmart >= 0.14])
AC_SUBST(LIBATASMART_CFLAGS)
AC_SUBST(LIBATASMART_LIBS)

PKG_CHECK_MODULES(LIBUDEV, [libudev >= 143])
AC_SUBST(LIBUDEV_CFLAGS)
AC_SUBST(LIBUDEV_LIBS)

if test "x$GCC" = "xyes"; then
  LDFLAGS="-Wl,--as-needed $LDFLAGS"
fi

if test "$prefix" = "/usr" -o "$prefix" = "/usr/local" ; then
  slashlibdir=/lib
  slashsbindir=/sbin
else
  slashlibdir=$prefix/lib
  slashsbindir=$prefix/sbin
fi
AC_SUBST(slashlibdir)
AC_SUBST(slashsbindir)

# *************
# Remote Access
# *************
remote_access=no
AC_ARG_ENABLE(remote_access, AS_HELP_STRING([--disable-remote-access], [disable remote access]))
if test "x$enable_remote_access" != "xno"; then
   AC_DEFINE(REMOTE_ACCESS_ENABLED, 1, [Defined if remote access is enabled])
   remote_access=yes
fi
AM_CONDITIONAL(REMOTE_ACCESS_ENABLED, [test "$remote_access" = "yes"])

# ********************
# Internationalization
# ********************

IT_PROG_INTLTOOL([0.36.0])
GETTEXT_PACKAGE=udisks
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

AC_OUTPUT([
Makefile
data/Makefile
data/udisks.pc
src/Makefile
src/helpers/Makefile
src/probers/Makefile
tools/Makefile
doc/Makefile
doc/version.xml
doc/man/Makefile
doc/dbus/Makefile
policy/Makefile
po/Makefile.in
tests/Makefile
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
        slashlibdir:                ${slashlibdir}
        slashsbindir:               ${slashsbindir}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}
        xsltproc:                   ${XSLTPROC}

        LVM2 support:               ${have_lvm2}
        dm-multipath:               ${have_dmmp}
        Remote Access:              ${remote_access}

        Maintainer mode:            ${USE_MAINTAINER_MODE}
        Profiling:                  ${enable_profiling}
        Building api docs:          ${enable_gtk_doc}
        Building man pages:         ${enable_man_pages}
"
