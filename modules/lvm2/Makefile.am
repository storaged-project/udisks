## Process this file with automake to produce Makefile.in

SUBDIRS = data

LVM2_MODULE_DIR = $(top_srcdir)/modules/lvm2

MODULE_SO = libudisks2_lvm2.so

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

$(dbus_built_sources): $(LVM2_MODULE_DIR)/data/org.freedesktop.UDisks2.lvm2.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen                                              \
	        --interface-prefix org.freedesktop.UDisks2.                    \
	        --c-namespace UDisks                                           \
	        --c-generate-autocleanup all                                   \
	        --generate-c-code udisks-lvm2-generated                        \
	        --generate-docbook udisks-generated-doc                        \
	        $<

dbus_built_sources =                                                           \
	udisks-lvm2-generated.h    udisks-lvm2-generated.c                     \
	$(NULL)

BUILT_SOURCES =                                                                \
	$(dbus_built_sources)                                                  \
	$(NULL)

libudisks2_lvm2dir=$(libdir)/udisks2/modules
libudisks2_lvm2_LTLIBRARIES=libudisks2_lvm2.la

libudisks2_lvm2includedir=$(includedir)/udisks2/udisks

libudisks2_lvm2include_HEADERS =                                               \
	$(top_srcdir)/modules/udisksmoduleiface.h                              \
	$(top_srcdir)/modules/udisksmoduleifacetypes.h                         \
	$(top_srcdir)/modules/udisksmoduleobject.h                             \
	$(NULL)

libudisks2_lvm2_la_SOURCES =                                                   \
	$(BUILT_SOURCES)                                                       \
	$(top_srcdir)/modules/udisksmoduleiface.h                              \
	$(top_srcdir)/modules/udisksmoduleifacetypes.h                         \
	$(top_srcdir)/modules/udisksmoduleobject.h                             \
	$(top_srcdir)/modules/udisksmoduleobject.c                             \
	udiskslvm2types.h                                                      \
	udiskslvm2moduleiface.c                                                \
	udiskslinuxlogicalvolume.h          udiskslinuxlogicalvolume.c         \
	udiskslinuxlogicalvolumeobject.h                                       \
	udiskslinuxlogicalvolumeobject.c                                       \
	udiskslinuxphysicalvolume.h         udiskslinuxphysicalvolume.c        \
	udiskslinuxvolumegroup.h            udiskslinuxvolumegroup.c           \
	udiskslinuxvolumegroupobject.h      udiskslinuxvolumegroupobject.c     \
	udiskslinuxblocklvm2.h              udiskslinuxblocklvm2.c             \
	udiskslvm2daemonutil.h              udiskslvm2daemonutil.c             \
	udiskslvm2dbusutil.h                udiskslvm2dbusutil.c               \
	udiskslvm2util.h                    udiskslvm2util.c                   \
	udiskslinuxmanagerlvm2.h            udiskslinuxmanagerlvm2.c           \
	udiskslvm2state.h                   udiskslvm2state.c                  \
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

# ------------------------------------------------------------------------------

CLEANFILES = udisks-generated-doc-*.xml udisks-generated.[ch]

EXTRA_DIST =                                                                   \
	$(NULL)

include ../Makefile.uninstalled

all-local: module_link

dist-hook:
	(for i in $(BUILT_SOURCES) ; do rm -f $(distdir)/$$i ; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
