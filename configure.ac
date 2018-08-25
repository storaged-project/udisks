
m4_define([udisks_major_version], [2])
m4_define([udisks_minor_version], [8])
m4_define([udisks_micro_version], [1])

m4_define([udisks_version], [udisks_major_version.udisks_minor_version.udisks_micro_version])

AC_INIT([udisks],[udisks_version],[https://storaged-project.github.io],[udisks])

UDISKS_MAJOR_VERSION=udisks_major_version
UDISKS_MINOR_VERSION=udisks_minor_version
UDISKS_MICRO_VERSION=udisks_micro_version
UDISKS_VERSION=udisks_version
AC_SUBST(UDISKS_MAJOR_VERSION)
AC_SUBST(UDISKS_MINOR_VERSION)
AC_SUBST(UDISKS_MICRO_VERSION)
AC_SUBST(UDISKS_VERSION)

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.11 foreign silent-rules dist-bzip2 no-dist-gzip])
AM_MAINTAINER_MODE
AM_SILENT_RULES([yes])

AX_CHECK_ENABLE_DEBUG([no], [DEBUG], [NDEBUG])

AC_PROG_CC
AC_PROG_CC_C99
AC_ISC_POSIX
AC_HEADER_STDC
AC_PROG_LIBTOOL


# Compilation
#

GNOME_COMPILE_WARNINGS([maximum])

CC_CHECK_CFLAGS_APPEND([                          \
        -Wall                                     \
        -W                                        \
        -Wextra                                   \
        -Wcast-align                              \
        -Wdeclaration-after-statement             \
        -Wendif-labels                            \
        -Werror=format-security                   \
        -Werror=implicit-function-declaration     \
        -Werror=overflow                          \
        -Wformat-nonliteral                       \
        -Wformat=2                                \
        -Wimplicit                                \
        -Winit-self                               \
        -Wlogical-op                              \
        -Wmissing-declarations                    \
        -Wmissing-format-attribute                \
        -Wmissing-include-dirs                    \
        -Wmissing-noreturn                        \
        -Wmissing-prototypes                      \
        -Wnested-externs                          \
        -Wno-inline                               \
        -Wno-long-long                            \
        -Wno-missing-field-initializers           \
        -Wno-overlength-strings                   \
        -Wno-switch-enum                          \
        -Wno-unused-parameter                     \
        -Wno-unused-result                        \
        -Wold-style-definition                    \
        -Wpacked                                  \
        -Wpointer-arith                           \
        -Wshadow                                  \
        -Wsign-compare                            \
        -Wstrict-aliasing=2                       \
        -Wstrict-prototypes                       \
        -Wundef                                   \
        -Wuninitialized                           \
        -Wmaybe-uninitialized                     \
        -Wunsafe-loop-optimizations               \
        -Wvla                                     \
        -Wwrite-strings                           \
        -fdata-sections                           \
        -fdiagnostics-show-option                 \
        -ffast-math                               \
        -ffunction-sections                       \
        -fno-common                               \
        -fno-strict-aliasing                      \
        -D_LARGEFILE64_SOURCE                     \
        -D_FILE_OFFSET_BITS=64                    \
])

if test -z "$enable_modules"; then
  enable_modules=no
fi
if test -z "$enable_available_modules"; then
  enable_available_modules=no
fi

GTK_DOC_CHECK([1.3],[--flavour no-tmpl])

AC_ARG_ENABLE(man,
              [AS_HELP_STRING([--enable-man],
                              [generate man pages [default=auto]])],,
              enable_man=maybe)
AS_IF([test "$enable_man" != no], [
  AC_PATH_PROG([XSLTPROC], [xsltproc])
  AS_IF([test -z "$XSLTPROC"], [
    AS_IF([test "$enable_man" = yes], [
      AC_MSG_ERROR([xsltproc is required for --enable-man])
    ])
    enable_man=no
  ])
  enable_man=yes
])
AM_CONDITIONAL(ENABLE_MAN, test "$enable_man" != no)

GOBJECT_INTROSPECTION_CHECK([0.6.2])

# Behavior
#

fhs_media=no
AC_ARG_ENABLE(fhs-media,
              [AS_HELP_STRING([--enable-fhs-media],
                              [Mount devices in /media instead of /run/media [default=no]])],
              AS_IF([test "x$enable_fhs_media" == "xyes"], [fhs_media=yes]),
              fhs_media=no)
if test "x$fhs_media" = "xyes"; then
  AC_DEFINE([HAVE_FHS_MEDIA], 1, [Define to 1 to use /media for mounting])
fi

# Libraries
#

PKG_CHECK_MODULES(GUDEV, [gudev-1.0 >= 165])
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.50])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GIO, [gio-unix-2.0 >= 2.50])
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(GMODULE, [gmodule-2.0])
AC_SUBST(GMODULE_CFLAGS)
AC_SUBST(GMODULE_LIBS)

PKG_CHECK_MODULES(BLOCKDEV, [blockdev >= 2.19])
AC_SUBST(BLOCKDEV_CFLAGS)
AC_SUBST(BLOCKDEV_LIBS)

PKG_CHECK_MODULES(POLKIT_GOBJECT_1, [polkit-gobject-1 >= 0.102])
AC_SUBST(POLKIT_GOBJECT_1_CFLAGS)
AC_SUBST(POLKIT_GOBJECT_1_LIBS)

PKG_CHECK_MODULES(POLKIT_AGENT_1, [polkit-agent-1 >= 0.102])
AC_SUBST(POLKIT_AGENT_1_CFLAGS)
AC_SUBST(POLKIT_AGENT_1_LIBS)

PKG_CHECK_MODULES(LIBATASMART, [libatasmart >= 0.17])
AC_SUBST(LIBATASMART_CFLAGS)
AC_SUBST(LIBATASMART_LIBS)

PKG_CHECK_MODULES(LIBMOUNT, [mount >= 2.30],
                  [have_libmount=yes],
                  [have_libmount=no])
AM_CONDITIONAL(HAVE_LIBMOUNT, test x$have_libmount = xyes)
if test "x$have_libmount" = "xyes"; then
  AC_DEFINE([HAVE_LIBMOUNT], 1, [Define to 1 if libmount >= 2.30 is available])
fi
AC_SUBST(HAVE_LIBMOUNT)
AC_SUBST(LIBMOUNT_CFLAGS)
AC_SUBST(LIBMOUNT_LIBS)

PKG_CHECK_MODULES(LIBSYSTEMD_LOGIN, [libsystemd >= 209], [have_libsystemd_login=yes],
                  [PKG_CHECK_MODULES(LIBSYSTEMD_LOGIN, [libsystemd-login >= 44 libsystemd-daemon],
                  [have_libsystemd_login=yes],
                  [have_libsystemd_login=no])])
AM_CONDITIONAL(HAVE_LIBSYSTEMD_LOGIN, test x$have_libsystemd_login = xyes)
if test "x$have_libsystemd_login" = "xyes"; then
  AC_DEFINE([HAVE_LIBSYSTEMD_LOGIN], 1, [Define to 1 if libsystemd-login is available])
fi
AC_SUBST(HAVE_LIBSYSTEMD_LOGIN)
AC_SUBST(LIBSYSTEMD_LOGIN_CFLAGS)
AC_SUBST(LIBSYSTEMD_LOGIN_LIBS)

PKG_CHECK_MODULES(LIBELOGIND, [libelogind >= 219],
                  [have_libelogind=yes],
                  [have_libelogins=no])
AM_CONDITIONAL(HAVE_LIBELOGIND, test x$have_libelogind = xyes)
if test "x$have_libelogind" = "xyes"; then
  AC_DEFINE([HAVE_LIBELOGIND], 1, [Define to 1 if libelogind is available])
fi
AC_SUBST(HAVE_LIBELOGIND)
AC_SUBST(LIBELOGIND_CFLAGS)
AC_SUBST(LIBELOGIND_LIBS)

# udevdir
AC_ARG_WITH([udevdir],
            AS_HELP_STRING([--with-udevdir=DIR], [Directory for udev]),
            [],
            [with_udevdir=$($PKG_CONFIG --variable=udevdir udev)])
AC_SUBST([udevdir], [$with_udevdir])

# systemd
AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
            [],
            [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != "xno"; then
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$systemdsystemunitdir"])

# kernel modules
AC_ARG_WITH([modloaddir],
  [AS_HELP_STRING([--with-modloaddir=DIR], [Directory for configuring kernel modules to load at boot])],
  [modloaddir=$withval],
  [modloaddir="/usr/lib/modules-load.d"])
AC_SUBST([modloaddir], [$modloaddir])

AC_ARG_WITH([modprobedir],
  [AS_HELP_STRING([--with-modprobedir=DIR], [Directory for modprobe configuration])],
  [modprobedir=$withval],
  [modprobedir="/usr/lib/modprobe.d"])
AC_SUBST([modprobedir], [$modprobedir])

have_acl=no
AC_ARG_ENABLE(acl, AS_HELP_STRING([--disable-acl], [disable acl support]))
if test "x$enable_acl" != "xno"; then
  AC_CHECK_HEADERS(
          [sys/acl.h acl/libacl.h],
          [
            AC_CHECK_LIB(
                [acl],
                [acl_get_file],
                [AC_DEFINE(HAVE_ACL, 1, [Define if libacl is available]) have_acl=yes],
                have_acl=no)
          ],
          have_acl=no)
  if test "x$have_acl" = "xyes"; then
    ACL_CFLAGS=""
    ACL_LIBS="-lacl"
  fi
  AC_SUBST(ACL_CFLAGS)
  AC_SUBST(ACL_LIBS)
  if test "x$have_acl" = xno -a "x$enable_acl" = xyes; then
    AC_MSG_ERROR([acl support requested but libraries not found])
  fi
fi
AM_CONDITIONAL(HAVE_ACL, [test "$have_acl" = "yes"])


# LVM2 module
have_lvm2=no
AC_ARG_ENABLE(lvm2, AS_HELP_STRING([--enable-lvm2], [enable LVM2 support]))
if test "x$enable_lvm2" = "xyes" \
     -o "x$enable_modules" = "xyes" \
     -o "x$enable_available_modules" = "xyes"; then

  SAVE_CFLAGS=$CFLAGS
  SAVE_LDFLAGS=$LDFLAGS

  CFLAGS="$GLIB_CFLAGS"
  LDFLAGS="$GLIB_LIBS"
  AC_MSG_CHECKING([libblockdev-lvm presence])
  AC_TRY_COMPILE([#include <blockdev/lvm.h>], [],
                 [AC_MSG_RESULT([yes])
                 have_lvm2=yes],
                 [AC_MSG_RESULT([no])
                 have_lvm2=no])
  CFLAGS=$SAVE_CFLAGS
  LDFLAGS=$SAVE_LDFLAGS

  if test "x$have_lvm2" = "xno"; then
     if test "x$enable_lvm2" = "xyes" -o "x$enable_modules" = "xyes"; then
         AC_MSG_ERROR([lvm2 support requested but libraries not found])
     fi
  fi
fi
AM_CONDITIONAL(HAVE_LVM2, [test "$have_lvm2" = "yes"])

# LVMCache
have_lvmcache=no
AC_ARG_ENABLE(lvmcache, AS_HELP_STRING([--enable-lvmcache], [enable LVMCache support]))
if test "x$enable_lvmcache" = "xyes" \
     -o "x$enable_modules" = "xyes" \
     -o "x$enable_available_modules" = "xyes"; then

  if test "x$have_lvm2" = "xno"; then
     if test "x$enable_lvmcache" = "xyes" -o "x$enable_modules" = "xyes"; then
         AC_MSG_ERROR([lvmcache support requested, but lvm2 module not enabled])
     fi
  fi
  have_lvmcache=yes
  AC_DEFINE(HAVE_LVMCACHE, 1, [Define if lvmcache is available])
fi
AM_CONDITIONAL(HAVE_LVMCACHE, [test "$have_lvmcache" = "yes"])

# iSCSI module
have_iscsi=no
have_libiscsi_session_info="no"
have_libiscsi_session_info_msg=""
AC_ARG_ENABLE(iscsi, AS_HELP_STRING([--enable-iscsi], [enable iSCSI support]))
if test "x$enable_iscsi" = "xyes" \
     -o "x$enable_modules" = "xyes" \
     -o "x$enable_available_modules" = "xyes"; then
  # libiscsi.h
  AC_CHECK_HEADER([libiscsi.h],
                  [AC_DEFINE(HAVE_ISCSI, 1, [Define if libiscsi from iscsi-initiator-utils is available])
                   have_iscsi=yes],
                  [have_iscsi=no])
  if test "x$have_iscsi" = "xyes"; then
    AC_MSG_CHECKING([whether libiscsi.h defines iscsi_err])
    AC_TRY_COMPILE([#include <libiscsi.h>],
                   [int err = ISCSI_SUCCESS;],
                   [AC_MSG_RESULT([yes])
                    AC_DEFINE([HAVE_LIBISCSI_ERR], [1], [libiscsi error states])],
                   [AC_MSG_RESULT([no])])
  fi

  # libiscsi
  AC_CHECK_LIB([iscsi],
               [libiscsi_init],
               [ISCSI_LIBS="-liscsi"],
               [have_iscsi="no"])
  if test "x$have_iscsi" = "xyes"; then
    AC_CHECK_LIB([iscsi],
                 [libiscsi_get_session_info_by_id],
                 [have_libiscsi_session_info="yes"
                  have_libiscsi_session_info_msg=", with iscsi sessions"
                  AC_DEFINE([HAVE_LIBISCSI_GET_SESSION_INFOS], [1],
                            [libiscsi can retrieve session information])],
                 [have_libiscsi_session_info_msg=", without iscsi sessions"])
  fi

  if test "x$have_iscsi" = "xno"; then
     if test "x$enable_iscsi" = "xyes" -o "x$enable_modules" = "xyes"; then
         AC_MSG_ERROR([iSCSI support requested but libraries not found])
     fi
  fi
  AC_SUBST([ISCSI_LIBS])
fi
AM_CONDITIONAL(HAVE_ISCSI, [test "x$have_iscsi" = "xyes"])
AM_CONDITIONAL(BUILD_ISCSI_SESSION_OBJECT, [test "x$have_libiscsi_session_info" = "xyes"])

# BTRFS module
have_btrfs=no
AC_ARG_ENABLE(btrfs, AS_HELP_STRING([--enable-btrfs], [enable BTRFS support]))
if test "x$enable_btrfs" = "xyes" \
     -o "x$enable_modules" = "xyes" \
     -o "x$enable_available_modules" = "xyes"; then
    # libblockdev
    SAVE_CFLAGS=$CFLAGS
    SAVE_LDFLAGS=$LDFLAGS
    CFLAGS="$GLIB_CFLAGS"
    LDFLAGS="$GLIB_LIBS"
    # We don't use AC_CHECK_HEADER to avoid these warnings:
    #   - accepted by the compiler, rejected by the preprocessor!
    #   - proceeding with the compiler's result
    AC_MSG_CHECKING(["libblockdev-btrfs presence"])
    AC_TRY_COMPILE([#include <blockdev/btrfs.h>], [],
                   [AC_MSG_RESULT([yes])
                    AC_DEFINE(HAVE_BTRFS, 1, [Define, if libblockdev-btrfs is available])
                    have_btrfs=yes],
                   [AC_MSG_RESULT([no])
                    have_btrfs=no])
    CFLAGS=$SAVE_CFLAGS
    LDFLAGS=$SAVE_LDFLAGS

    if test "x$have_btrfs" = "xno"; then
     if test "x$enable_btrfs" = "xyes" -o "x$enable_modules" = "xyes"; then
         AC_MSG_ERROR([BTRFS support requested but header or library not found])
     fi
    fi
fi
AM_CONDITIONAL(HAVE_BTRFS, [test "x$have_btrfs" = "xyes"])

# ZRAM module
have_kbd="no"
have_swap="no"
have_zram="no"
AC_ARG_ENABLE(zram, AS_HELP_STRING([--enable-zram], [enable ZRAM support]))
if test "x$enable_zram" = "xyes" \
     -o "x$enable_modules" = "xyes" \
     -o "x$enable_available_modules" = "xyes"; then
    # libblockdev
    SAVE_CFLAGS=$CFLAGS
    SAVE_LDFLAGS=$LDFLAGS
    CFLAGS="$GLIB_CFLAGS"
    LDFLAGS="$GLIB_LIBS"

    # libblockdev-kbd
    AC_MSG_CHECKING([libblockdev-kbd presence])
    AC_TRY_COMPILE([#include <blockdev/kbd.h>], [],
                   [AC_MSG_RESULT([yes])
                    have_kbd=yes],
                   [AC_MSG_RESULT([no])
                    have_kbd=no])

    if test "x$have_kbd" = "xno"; then
        if test "x$enable_zram" = "xyes" -o "x$enable_modules" = "xyes"; then
            AC_MSG_ERROR([KBD support requested but header or library not found])
        fi
    fi

    CFLAGS="$GLIB_CFLAGS"
    LDFLAGS="$GLIB_LIBS"

    # libblockdev-swap
    AC_MSG_CHECKING([libblockdev-swap presence])
    AC_TRY_COMPILE([#include <blockdev/swap.h>], [],
                   [AC_MSG_RESULT([yes])
                   have_swap=yes],
                   [AC_MSG_RESULT([no])
                   have_swap=no])

    CFLAGS=$SAVE_CFLAGS
    LDFLAGS=$SAVE_LDFLAGS

    if test "x$have_swap" = "xno"; then
        if test "x$enable_zram" = "xyes" -o "x$enable_modules" = "xyes"; then
            AC_MSG_ERROR([SWAP support requested but header or library not found])
        fi
    fi

    have_zram="yes"

    if test "$have_kbd" = "yes" -a "$have_swap" = "yes"; then
       AC_DEFINE(HAVE_ZRAM, 1, [Define, if libblockdev-kbd and libblockdev-swap are available])
    fi
fi
AM_CONDITIONAL(HAVE_ZRAM, [test "$have_kbd" = "yes" -a "$have_swap" = "yes"])


# LSM module
have_lsm=no
have_libconfig=no
AC_ARG_ENABLE(
    lsm, AS_HELP_STRING([--enable-lsm], [enable LibStorageMgmt support]))
if test "x$enable_lsm" = "xyes" \
     -o "x$enable_modules" = "xyes" \
     -o "x$enable_available_modules" = "xyes"; then
    PKG_CHECK_MODULES(
        [LIBLSM], [libstoragemgmt >= 1.3.0],
        [AC_DEFINE(HAVE_LSM, 1, [Define if liblibstoragemgmt is available])
            have_lsm=yes],
        [AC_MSG_WARN([libstoragemgmt 1.3.0 or newer not found.])
            have_lsm=no])

    PKG_CHECK_MODULES(
        [LIBCONFIG], [libconfig >= 1.3.2],
        [have_libconfig=yes],
        [AC_MSG_WARN([libconfig 1.3.2 or newer not found.])
            have_libconfig=no])

  if test "x$have_libconfig" = "xno"; then
      have_lsm=no
  fi

  if test "x$have_lsm" = "xno"; then
      if test "x$enable_lsm" = "xyes" -o "x$enable_modules" = "xyes"; then
          AC_MSG_ERROR([LibStorageMgmt support requested but libraries not found])
      fi
  fi
fi
AM_CONDITIONAL(HAVE_LSM, [test "x$have_lsm" = "xyes"])

# Bcache module
have_bcache=no
AC_ARG_ENABLE(bcache, AS_HELP_STRING([--enable-bcache], [enable Bcache support]))
if test "x$enable_bcache" = "xyes" \
     -o "x$enable_modules" = "xyes" \
     -o "x$enable_available_modules" = "xyes"; then
    # libblockdev
    SAVE_CFLAGS=$CFLAGS
    SAVE_LDFLAGS=$LDFLAGS
    CFLAGS="$GLIB_CFLAGS"
    LDFLAGS="$GLIB_LIBS"

    # libblockdev-kbd
    AC_MSG_CHECKING([libblockdev-kbd presence])
    AC_TRY_COMPILE([#include <blockdev/kbd.h>], [],
                   [AC_MSG_RESULT([yes])
                    have_bcache=yes],
                   [AC_MSG_RESULT([no])
                    have_bcache=no])
    CFLAGS=$SAVE_CFLAGS
    LDFLAGS=$SAVE_LDFLAGS

    if test "x$have_bcache" = "xno"; then
        if test "x$enable_bcache" = "xyes" -o "x$enable_modules" = "xyes"; then
            AC_MSG_ERROR([Bcache support requested but header or library not found])
        fi
    fi
fi
AM_CONDITIONAL(HAVE_BCACHE, [test "x$have_bcache" = "xyes"])

# VDO module
have_vdo=no
AC_ARG_ENABLE(vdo, AS_HELP_STRING([--enable-vdo], [enable VDO support]))
if test "x$enable_vdo" = "xyes" \
     -o "x$enable_modules" = "xyes" \
     -o "x$enable_available_modules" = "xyes"; then
    # libblockdev
    SAVE_CFLAGS=$CFLAGS
    SAVE_LDFLAGS=$LDFLAGS
    CFLAGS="$GLIB_CFLAGS"
    LDFLAGS="$GLIB_LIBS"

    # libblockdev-vdo
    AC_MSG_CHECKING([libblockdev-vdo presence])
    AC_TRY_COMPILE([#include <blockdev/vdo.h>], [],
                   [AC_MSG_RESULT([yes])
                    have_vdo=yes],
                   [AC_MSG_RESULT([no])
                    have_vdo=no])
    CFLAGS=$SAVE_CFLAGS
    LDFLAGS=$SAVE_LDFLAGS

    if test "x$have_vdo" = "xno"; then
        if test "x$enable_vdo" = "xyes" -o "x$enable_modules" = "xyes"; then
            AC_MSG_ERROR([VDO support requested but header or library not found])
        fi
    fi
fi
AM_CONDITIONAL(HAVE_VDO, [test "x$have_vdo" = "xyes"])

# libblockdev part
SAVE_CFLAGS=$CFLAGS
SAVE_LDFLAGS=$LDFLAGS

CFLAGS="$GLIB_CFLAGS"
LDFLAGS="$GLIB_LIBS"
AC_MSG_CHECKING([libblockdev-part presence])
AC_TRY_COMPILE([#include <blockdev/part.h>], [],
               [AC_MSG_RESULT([yes])
               have_part=yes],
               [AC_MSG_RESULT([no])
               have_part=no])

CFLAGS=$SAVE_CFLAGS
LDFLAGS=$SAVE_LDFLAGS

if test "x$have_part" = "xno"; then
  AC_MSG_ERROR([BlockDev part support requested but header or library not found])
fi

# libblockdev loop
SAVE_CFLAGS=$CFLAGS
SAVE_LDFLAGS=$LDFLAGS

CFLAGS="$GLIB_CFLAGS"
LDFLAGS="$GLIB_LIBS"
AC_MSG_CHECKING([libblockdev-loop presence])
AC_TRY_COMPILE([#include <blockdev/loop.h>], [],
               [AC_MSG_RESULT([yes])
               have_loop=yes],
               [AC_MSG_RESULT([no])
               have_loop=no])

CFLAGS=$SAVE_CFLAGS
LDFLAGS=$SAVE_LDFLAGS

if test "x$have_loop" = "xno"; then
  AC_MSG_ERROR([Loop support requested but header or library not found])
fi

# libblockdev swap
SAVE_CFLAGS=$CFLAGS
SAVE_LDFLAGS=$LDFLAGS

CFLAGS="$GLIB_CFLAGS"
LDFLAGS="$GLIB_LIBS"
AC_MSG_CHECKING([libblockdev-swap presence])
AC_TRY_COMPILE([#include <blockdev/swap.h>], [],
               [AC_MSG_RESULT([yes])
               have_swap=yes],
               [AC_MSG_RESULT([no])
               have_swap=no])

CFLAGS=$SAVE_CFLAGS
LDFLAGS=$SAVE_LDFLAGS

if test "x$have_swap" = "xno"; then
  AC_MSG_ERROR([SWAP support requested but header or library not found])
fi

# libblockdev mdraid
SAVE_CFLAGS=$CFLAGS
SAVE_LDFLAGS=$LDFLAGS

CFLAGS="$GLIB_CFLAGS"
LDFLAGS="$GLIB_LIBS"
AC_MSG_CHECKING([libblockdev-mdraid presence])
AC_TRY_COMPILE([#include <blockdev/mdraid.h>], [],
               [AC_MSG_RESULT([yes])
               have_mdraid=yes],
               [AC_MSG_RESULT([no])
               have_mdraid=no])

CFLAGS=$SAVE_CFLAGS
LDFLAGS=$SAVE_LDFLAGS

if test "x$have_mdraid" = "xno"; then
  AC_MSG_ERROR([BLOCKDEV MDRAID support requested but header or library not found])
fi

# libblockdev fs
SAVE_CFLAGS=$CFLAGS
SAVE_LDFLAGS=$LDFLAGS

CFLAGS="$GLIB_CFLAGS"
LDFLAGS="$GLIB_LIBS"
AC_MSG_CHECKING([libblockdev-fs presence])
AC_TRY_COMPILE([#include <blockdev/fs.h>], [],
               [AC_MSG_RESULT([yes])
               have_fs=yes],
               [AC_MSG_RESULT([no])
               have_fs=no])

CFLAGS=$SAVE_CFLAGS
LDFLAGS=$SAVE_LDFLAGS

if test "x$have_fs" = "xno"; then
  AC_MSG_ERROR([BLOCKDEV FS support requested but header or library not found])
fi

# libblockdev crypto
SAVE_CFLAGS=$CFLAGS
SAVE_LDFLAGS=$LDFLAGS

CFLAGS="$GLIB_CFLAGS"
LDFLAGS="$GLIB_LIBS"
AC_MSG_CHECKING([libblockdev-crypto presence])
AC_TRY_COMPILE([#include <blockdev/crypto.h>], [],
               [AC_MSG_RESULT([yes])
               have_crypto=yes],
               [AC_MSG_RESULT([no])
               have_crypto=no])

CFLAGS=$SAVE_CFLAGS
LDFLAGS=$SAVE_LDFLAGS

if test "x$have_crypto" = "xno"; then
  AC_MSG_ERROR([BLOCKDEV CRYPTO support requested but header or library not found])
fi


# Internationalization
#

IT_PROG_INTLTOOL([$INTLTOOL_REQUIRED])
GETTEXT_PACKAGE=udisks2
AC_SUBST([GETTEXT_PACKAGE])
AM_GLIB_GNU_GETTEXT
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"],[gettext domain])

AC_DEFINE([PROJECT_SYSCONF_DIR], ["udisks2"], [Project configuration directory])
AC_DEFINE([PACKAGE_NAME_UDISKS2], ["udisks2"], [Full package name for UDisks2 compatibility])

# Generate
#

if test "x$enable_modules" = "xyes"; then
  enable_modules_info=yes
elif test "x$enable_available_modules" = "xyes"; then
    enable_modules_info=available
else
  enable_modules_info=no
fi

AC_OUTPUT([
Makefile
data/Makefile
udisks/Makefile
udisks/udisks2.conf
udisks/udisks2.pc
udisks/modules.conf.d/Makefile
udisks/modules.conf.d/udisks2_lsm.conf
src/Makefile
src/tests/Makefile
tools/Makefile
modules/Makefile
modules/btrfs/Makefile
modules/btrfs/data/Makefile
modules/iscsi/Makefile
modules/iscsi/data/Makefile
modules/lvm2/Makefile
modules/lvm2/data/Makefile
modules/zram/Makefile
modules/zram/data/Makefile
modules/lsm/Makefile
modules/lsm/data/Makefile
modules/bcache/Makefile
modules/bcache/data/Makefile
modules/vdo/Makefile
modules/vdo/data/Makefile
doc/Makefile
doc/udisks2-docs.xml.in
doc/udisks2-sections.txt.in
doc/udisks2.types.in
doc/version.xml
doc/man/Makefile
doc/man/udisks.xml
doc/man/udisksd.xml
doc/man/udisksctl.xml
doc/man/umount.udisks2.xml
doc/man/udisks2.conf.5.xml.in
doc/man/udisks2_lsm.conf.5.xml.in
doc/udisks2-docs.xml.iscsi.dbus
doc/udisks2-docs.xml.iscsi.generated
doc/udisks2-sections.txt.iscsi.sections
doc/udisks2.types.iscsi
doc/udisks2-docs.xml.lsm.man
doc/udisks2-docs.xml.lsm.dbus
doc/udisks2-docs.xml.lsm.generated
doc/udisks2-sections.txt.lsm.sections
doc/udisks2.types.lsm
doc/udisks2-docs.xml.lvm2.dbus
doc/udisks2-docs.xml.lvm2.generated
doc/udisks2-sections.txt.lvm2.sections
doc/udisks2.types.lvm2
doc/udisks2-docs.xml.btrfs.dbus
doc/udisks2-docs.xml.btrfs.generated
doc/udisks2-sections.txt.btrfs.sections
doc/udisks2.types.btrfs
doc/udisks2-docs.xml.zram.dbus
doc/udisks2-docs.xml.zram.generated
doc/udisks2-docs.xml.zram.block_devices
doc/udisks2-sections.txt.zram.sections
doc/udisks2.types.zram
doc/udisks2-docs.xml.bcache.dbus
doc/udisks2-docs.xml.bcache.generated
doc/udisks2-sections.txt.bcache.sections
doc/udisks2.types.bcache
doc/udisks2-docs.xml.vdo.dbus
doc/udisks2-docs.xml.vdo.generated
doc/udisks2-sections.txt.vdo.sections
doc/udisks2.types.vdo
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
        introspection:              ${found_introspection}

        udevdir:                    ${udevdir}
        systemdsystemunitdir:       ${systemdsystemunitdir}
        using libsystemd-login:     ${have_libsystemd_login}
        using libelogind:           ${have_libelogind}
        use /media for mounting:    ${fhs_media}
        acl support:                ${have_acl}
        using libmount:             ${have_libmount}

        compiler:                   ${CC}
        cflags:                     ${CFLAGS}
        cppflags:                   ${CPPFLAGS}

        Maintainer mode:            ${USE_MAINTAINER_MODE}
        Building api docs:          ${enable_gtk_doc}
        Building man pages:         ${enable_man}

        Enable modules:             ${enable_modules_info}

        BTRFS module:               ${have_btrfs}
        iSCSI module:               ${have_iscsi}${have_libiscsi_session_info_msg}
        LVM2  module:               ${have_lvm2}
        LVMCache:                   ${have_lvmcache}
        Zram module:                ${have_zram}
        LibStorageMgmt module:      ${have_lsm}
        Bcache module:              ${have_bcache}
        VDO module:                 ${have_vdo}
"
