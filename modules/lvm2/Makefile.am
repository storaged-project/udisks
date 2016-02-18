## Process this file with automake to produce Makefile.in

SUBDIRS = data

LVM2_MODULE_DIR = $(top_srcdir)/modules/lvm2

MODULE_SO = libstoraged_lvm2.so

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
	-DLVM_HELPER_DIR=\""$(prefix)/lib/storaged/"\"                         \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                                \
	-DSTORAGED_COMPILATION                                                 \
	-DBUILD_DIR=\"$(abs_top_builddir)/\"                                   \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(WARN_CFLAGS)                                                         \
	$(BLOCKDEV_CFLAGS)                                                     \
	$(NULL)

$(dbus_built_sources): $(LVM2_MODULE_DIR)/data/org.storaged.Storaged.lvm2.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen                                              \
	        --interface-prefix org.storaged.Storaged.                      \
	        --c-namespace Storaged                                         \
	        --generate-c-code storaged-lvm2-generated                      \
	        --generate-docbook storaged-generated-doc                      \
	        $<

dbus_built_sources =                                                           \
	storaged-lvm2-generated.h    storaged-lvm2-generated.c                 \
	$(NULL)

BUILT_SOURCES =                                                                \
	$(dbus_built_sources)                                                  \
	$(NULL)

libstoraged_lvm2dir=$(libdir)/storaged/modules
libstoraged_lvm2_LTLIBRARIES=libstoraged_lvm2.la

libstoraged_lvm2includedir=$(includedir)/storaged/storaged

libstoraged_lvm2include_HEADERS =                                              \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(NULL)

libstoraged_lvm2_la_SOURCES =                                                  \
	$(BUILT_SOURCES)                                                       \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(top_srcdir)/modules/storagedmoduleobject.c                           \
	storagedlvm2types.h                                                    \
	storagedlvm2moduleiface.c                                              \
	storagedlinuxlogicalvolume.h          storagedlinuxlogicalvolume.c     \
	storagedlinuxlogicalvolumeobject.h                                     \
	storagedlinuxlogicalvolumeobject.c                                     \
	storagedlinuxphysicalvolume.h         storagedlinuxphysicalvolume.c    \
	storagedlinuxvolumegroup.h            storagedlinuxvolumegroup.c       \
	storagedlinuxvolumegroupobject.h      storagedlinuxvolumegroupobject.c \
	storagedlinuxblocklvm2.h              storagedlinuxblocklvm2.c         \
	storagedlvm2daemonutil.h              storagedlvm2daemonutil.c         \
	storagedlvm2dbusutil.h                storagedlvm2dbusutil.c           \
	storagedlvm2util.h                    storagedlvm2util.c               \
	storagedlinuxmanagerlvm2.h            storagedlinuxmanagerlvm2.c       \
	storagedlvm2state.h                   storagedlvm2state.c              \
	$(NULL)

libstoraged_lvm2_la_CPPFLAGS =                                                 \
	$(CPPFLAGS)                                                            \
	-DG_LOG_DOMAIN=\"libstoraged-lvm2\"                                    \
	$(NULL)

libstoraged_lvm2_la_CFLAGS =                                                   \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(BLOCKDEV_CFLAGS)                                                     \
	$(NULL)

libstoraged_lvm2_la_LDFLAGS =                                                  \
	-export_dynamic                                                        \
	-avoid-version                                                         \
	-module                                                                \
	-no-undefined                                                          \
	-export-symbols-regex                                                  \
	 '^storaged_.*|g_io_module_load|g_io_module_unload|g_io_module_query'  \
	$(NULL)

libstoraged_lvm2_la_LIBADD =                                                   \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(BLOCKDEV_LIBS)                                                       \
	$(NULL)

# ------------------------------------------------------------------------------

storageddprivdir = $(prefix)/lib/storaged
storageddpriv_PROGRAMS = storaged-lvm

storaged_lvm_SOURCES =                                                         \
	storagedlvmhelper.c                                                    \
	$(NULL)

storaged_lvm_CPPFLAGS =                                                        \
	$(CPPFLAGS)                                                            \
	$(NULL)

storaged_lvm_CFLAGS =                                                          \
	-DG_LOG_DOMAIN=\"storaged-lvm-helper\"                                 \
	$(DEVMAPPER_CFLAGS)                                                    \
	$(LVM2_CFLAGS)                                                         \
	$(BLOCKDEV_CFLAGS)                                                     \
	$(NULL)

storaged_lvm_LDADD =                                                           \
	$(GLIB_LIBS)                                                           \
	$(DEVMAPPER_LIBS)                                                      \
	$(LVM2_LIBS)                                                           \
	$(BLOCKDEV_LIBS)                                                       \
	$(NULL)

# ------------------------------------------------------------------------------

CLEANFILES = storaged-generated-doc-*.xml storaged-generated.[ch]

EXTRA_DIST =                                                                   \
	$(NULL)

include ../Makefile.uninstalled

all-local: module_link

dist-hook:
	(for i in $(BUILT_SOURCES) ; do rm -f $(distdir)/$$i ; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
