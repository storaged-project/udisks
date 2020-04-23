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
	-DLVM_HELPER_DIR=\""$(prefix)/lib/udisks2/"\"                          \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                                \
	-DUDISKS_COMPILATION                                                   \
	-DBUILD_DIR=\"$(abs_top_builddir)/\"                                   \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

if ENABLE_DAEMON

LVM2_MODULE_DIR = $(top_srcdir)/modules/lvm2

MODULE_SO = libudisks2_lvm2.so

BUILT_SOURCES =                                                                \
	$(NULL)

libudisks2_lvm2dir = $(libdir)/udisks2/modules
libudisks2_lvm2_LTLIBRARIES = libudisks2_lvm2.la

libudisks2_lvm2includedir = $(includedir)/udisks2/udisks

libudisks2_lvm2_la_SOURCES =                                                   \
	$(BUILT_SOURCES)                                                       \
	udiskslvm2types.h                                                      \
	udiskslinuxmodulelvm2.h             udiskslinuxmodulelvm2.c            \
	udiskslinuxlogicalvolume.h          udiskslinuxlogicalvolume.c         \
	udiskslinuxlogicalvolumeobject.h    udiskslinuxlogicalvolumeobject.c   \
	udiskslinuxphysicalvolume.h         udiskslinuxphysicalvolume.c        \
	udiskslinuxvolumegroup.h            udiskslinuxvolumegroup.c           \
	udiskslinuxvolumegroupobject.h      udiskslinuxvolumegroupobject.c     \
	udiskslinuxvdovolume.h              udiskslinuxvdovolume.c             \
	udiskslinuxblocklvm2.h              udiskslinuxblocklvm2.c             \
	udiskslvm2daemonutil.h              udiskslvm2daemonutil.c             \
	udiskslinuxmanagerlvm2.h            udiskslinuxmanagerlvm2.c           \
	jobhelpers.h                        jobhelpers.c                       \
	$(NULL)

libudisks2_lvm2_la_CPPFLAGS =                                                  \
	$(CPPFLAGS)                                                            \
	-DG_LOG_DOMAIN=\"libudisks2-lvm2\"                                     \
	$(NULL)

libudisks2_lvm2_la_CFLAGS =                                                    \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(NULL)

libudisks2_lvm2_la_LDFLAGS =                                                   \
	-export_dynamic                                                        \
	-avoid-version                                                         \
	-module                                                                \
	-no-undefined                                                          \
	-export-symbols-regex                                                  \
	 '^udisks_.*|g_io_module_load|g_io_module_unload|g_io_module_query'    \
	$(NULL)

libudisks2_lvm2_la_LIBADD =                                                    \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(NULL)

all-local: module_link

endif # ENABLE_DAEMON

# ------------------------------------------------------------------------------

CLEANFILES =

EXTRA_DIST =                                                                   \
	$(NULL)

include ../Makefile.uninstalled

dist-hook:
	(for i in $(BUILT_SOURCES) ; do rm -f $(distdir)/$$i ; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
