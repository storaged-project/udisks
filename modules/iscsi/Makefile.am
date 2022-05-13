## Process this file with automake to produce Makefile.in

SUBDIRS = data

NULL =

CPPFLAGS =                                                                     \
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
	$(WARN_CFLAGS)                                                         \
	$(NULL)

if ENABLE_DAEMON

ISCSI_MODULE_DIR = $(top_srcdir)/modules/iscsi

MODULE_SO = libudisks2_iscsi.so

BUILT_SOURCES =                                                                \
	$(NULL)

libudisks2_iscsidir = $(libdir)/udisks2/modules
libudisks2_iscsi_LTLIBRARIES = libudisks2_iscsi.la

libudisks2_iscsiincludedir = $(includedir)/udisks2/udisks

libudisks2_iscsi_la_SOURCES =                                                  \
	$(BUILT_SOURCES)                                                       \
	udiskslinuxmoduleiscsi.h                                               \
	udiskslinuxmoduleiscsi.c                                               \
	udiskslinuxmanageriscsiinitiator.h                                     \
	udiskslinuxmanageriscsiinitiator.c                                     \
	udisksiscsitypes.h                                                     \
	udisksiscsiutil.h                                                      \
	udisksiscsiutil.c                                                      \
	$(NULL)

if BUILD_ISCSI_SESSION_OBJECT
libudisks2_iscsi_la_SOURCES +=                                                 \
	udiskslinuxiscsisession.h                                              \
	udiskslinuxiscsisession.c                                              \
	udiskslinuxiscsisessionobject.h                                        \
	udiskslinuxiscsisessionobject.c
endif # BUILD_ISCSI_SESSION_OBJECT

libudisks2_iscsi_la_CPPFLAGS =                                                 \
	$(CPPFLAGS)                                                            \
	-DG_LOG_DOMAIN=\"libudisks2-iscsi\"                                    \
	$(NULL)

libudisks2_iscsi_la_CFLAGS =                                                   \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(NULL)

libudisks2_iscsi_la_LDFLAGS =                                                  \
	-export_dynamic                                                        \
	-avoid-version                                                         \
	-module                                                                \
	-no-undefined                                                          \
	-export-symbols-regex                                                  \
	 '^udisks_.*|g_io_module_load|g_io_module_unload|g_io_module_query'    \
	$(NULL)

libudisks2_iscsi_la_LIBADD =                                                   \
	$(top_builddir)/src/libudisks-daemon.la                                \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(ISCSI_LIBS)                                                          \
	$(NULL)

all-local: module_link

endif # ENABLE_DAEMON

# ------------------------------------------------------------------------------

CLEANFILES =

EXTRA_DIST =                                                                   \
	$(NULL)

include ../Makefile.uninstalled

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
