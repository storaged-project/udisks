## Process this file with automake to produce Makefile.in

SUBDIRS = data

NULL =

AM_CPPFLAGS =                                                                  \
	-I$(top_builddir) -I$(top_srcdir)                                      \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"                              \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"                              \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"                                    \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\"                                      \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\"                        \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\"                                \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\"                                      \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                                \
	-DUDISKS_COMPILATION                                                   \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(BLOCKDEV_CFLAGS)                                                     \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

if ENABLE_DAEMON

MODULE_SO = libudisks2_lsm.so

BUILT_SOURCES =                                                                \
	$(NULL)

libudisks2_lsmdir = $(libdir)/udisks2/modules
libudisks2_lsm_LTLIBRARIES = libudisks2_lsm.la

libudisks2_lsmincludedir = $(includedir)/udisks2/udisks

libudisks2_lsm_la_SOURCES =                                                    \
	$(BUILT_SOURCES)                                                       \
	udiskslinuxmodulelsm.h                                                 \
	udiskslinuxmodulelsm.c                                                 \
	lsm_types.h                                                            \
	lsm_linux_drive.h                                                      \
	lsm_linux_drive.c                                                      \
	lsm_linux_drive_local.h                                                \
	lsm_linux_drive_local.c                                                \
	lsm_data.h lsm_data.c                                                  \
	$(NULL)

libudisks2_lsm_la_CPPFLAGS =                                                   \
	$(AM_CPPFLAGS)                                                         \
	-DG_LOG_DOMAIN=\"libudisks2-lsm\"                                      \
	$(NULL)

libudisks2_lsm_la_CFLAGS =                                                     \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(LIBLSM_CFLAGS)                                                       \
	$(LIBCONFIG_CFLAGS)                                                    \
	$(NULL)

libudisks2_lsm_la_LDFLAGS =                                                    \
	-export_dynamic                                                        \
	-avoid-version                                                         \
	-module                                                                \
	-no-undefined                                                          \
	-export-symbols-regex                                                  \
	 '^udisks_.*|g_io_module_load|g_io_module_unload|g_io_module_query'    \
	$(NULL)

libudisks2_lsm_la_LIBADD =                                                     \
	$(top_builddir)/src/libudisks-daemon.la                                \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(LIBLSM_LIBS)                                                         \
	$(LIBCONFIG_LIBS)                                                      \
	$(NULL)

all-local: module_link

endif # ENABLE_DAEMON

# ------------------------------------------------------------------------------

CLEANFILES =

EXTRA_DIST = \
	$(NULL)

include ../Makefile.uninstalled

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ \
		$(BUILT_SOURCES)
