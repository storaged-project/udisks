## Process this file with automake to produce Makefile.in

SUBDIRS = data

BTRFS_MODULE_DIR = $(top_srcdir)/modules/btrfs

MODULE_SO = libstoraged_btrfs.so

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
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

$(dbus_built_sources): $(BTRFS_MODULE_DIR)/data/org.storaged.Storaged.btrfs.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen                                              \
	        --interface-prefix org.storaged.Storaged.                      \
	        --c-namespace Storaged                                         \
	        --generate-c-code storaged-btrfs-generated                     \
	        --generate-docbook storaged-generated-doc                      \
	        $<

dbus_built_sources =                                                           \
	storaged-btrfs-generated.h        storaged-btrfs-generated.c           \
	$(NULL)

BUILT_SOURCES =                                                                \
	$(dbus_built_sources)                                                  \
	$(NULL)

libstoraged_btrfsdir=$(libdir)/storaged/modules
libstoraged_btrfs_LTLIBRARIES=libstoraged_btrfs.la

libstoraged_btrfsincludedir=$(includedir)/storaged/storaged

libstoraged_btrfsinclude_HEADERS =                                             \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(NULL)

libstoraged_btrfs_la_SOURCES =                                                 \
	$(BUILT_SOURCES)                                                       \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(top_srcdir)/modules/storagedmoduleobject.c                           \
	storagedlinuxfilesystembtrfs.h                                         \
	storagedlinuxfilesystembtrfs.c                                         \
	storagedlinuxmanagerbtrfs.h                                            \
	storagedlinuxmanagerbtrfs.c                                            \
	storagedbtrfsstate.h                                                   \
	storagedbtrfsstate.c                                                   \
	storagedbtrfstypes.h                                                   \
	storagedbtrfsmoduleiface.c                                             \
	storagedbtrfsutil.h                                                    \
	storagedbtrfsutil.c                                                    \
	$(NULL)

libstoraged_btrfs_la_CPPFLAGS =                                                \
	$(CPPFLAGS)                                                            \
	-DG_LOG_DOMAIN=\"libstoraged-btrfs\"                                   \
	$(NULL)

libstoraged_btrfs_la_CFLAGS =                                                  \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(BTRFS_CFLAGS)                                                        \
	$(NULL)

libstoraged_btrfs_la_LDFLAGS =                                                 \
	-export_dynamic                                                        \
	-avoid-version                                                         \
	-module                                                                \
	-no-undefined                                                          \
	-export-symbols-regex                                                  \
	 '^storaged_.*|g_io_module_load|g_io_module_unload|g_io_module_query'  \
	$(NULL)

libstoraged_btrfs_la_LIBADD =                                                  \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(BTRFS_LIBS)                                                          \
	$(NULL)

# ------------------------------------------------------------------------------

CLEANFILES = storaged-generated-doc-*.xml storaged-generated.[ch]

EXTRA_DIST =                                                                   \
	$(NULL)

include ../Makefile.uninstalled

all-local: module_link

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
