## Process this file with automake to produce Makefile.in

SUBDIRS = data

BCACHE_MODULE_DIR = $(top_srcdir)/modules/bcache

MODULE_SO = libstoraged_bcache.so

NULL =

CPPFLAGS =                                                                    \
	-I$(top_builddir) -I$(top_srcdir)                                     \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"                             \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"                             \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"                                   \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\"                                     \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\"                       \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\"                               \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\"                                     \
	-DLVM_HELPER_DIR=\""$(prefix)/lib/storaged/"\"                        \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                               \
	-DSTORAGED_COMPILATION                                                \
	$(POLKIT_GOBJECT_1_CFLAGS)                                            \
	$(GLIB_CFLAGS)                                                        \
	$(GIO_CFLAGS)                                                         \
	$(GUDEV_CFLAGS)                                                       \
	$(WARN_CFLAGS)                                                        \
	$(NULL)

$(dbus_built_sources): $(BCACHE_MODULE_DIR)/data/org.storaged.Storaged.bcache.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen                                             \
	        --interface-prefix org.storaged.Storaged.                     \
	        --c-namespace Storaged                                        \
	        --generate-c-code storaged-bcache-generated                   \
	        --generate-docbook storaged-generated-doc                     \
	        $<

dbus_built_sources =                                                          \
	storaged-bcache-generated.h	storaged-bcache-generated.c           \
	$(NULL)

BUILT_SOURCES =                                                               \
	$(dbus_built_sources)                                                 \
	$(NULL)

libstoraged_bcachedir=$(libdir)/storaged/modules
libstoraged_bcache_LTLIBRARIES=libstoraged_bcache.la

libstoraged_bcacheincludedir=$(includedir)/storaged/storaged

libstoraged_bcacheinclude_HEADERS =                                           \
	$(top_srcdir)/modules/storagedmoduleiface.h                           \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                      \
	$(top_srcdir)/modules/storagedmoduleobject.h                          \
	$(NULL)

libstoraged_bcache_la_SOURCES =                                               \
	$(BUILT_SOURCES)                                                      \
	$(top_srcdir)/modules/storagedmoduleiface.h                           \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                      \
	$(top_srcdir)/modules/storagedmoduleobject.h                          \
	$(top_srcdir)/modules/storagedmoduleobject.c                          \
	storagedlinuxblockbcache.c                                            \
	storagedlinuxblockbcache.h                                            \
	storagedlinuxmanagerbcache.h                                          \
	storagedlinuxmanagerbcache.c                                          \
	storagedbcachemoduleiface.c                                           \
	storagedbcachestate.h                                                 \
	storagedbcachestate.c                                                 \
	storagedbcachetypes.h                                                 \
	storagedbcacheutil.c                                                  \
	storagedbcacheutil.h                                                  \
	$(NULL)

libstoraged_bcache_la_CPPFLAGS =                                              \
	$(CPPFLAGS)                                                           \
	-DG_LOG_DOMAIN=\"libstoraged-bcache\"                                 \
	$(NULL)

libstoraged_bcache_la_CFLAGS =                                                \
	$(GLIB_CFLAGS)                                                        \
	$(GIO_CFLAGS)                                                         \
	$(GUDEV_CFLAGS)                                                       \
	$(POLKIT_GOBJECT_1_CFLAGS)                                            \
	$(BCACHE_CFLAGS)                                                      \
	$(NULL)

libstoraged_bcache_la_LDFLAGS =                                               \
	-export_dynamic                                                       \
	-avoid-version                                                        \
	-module                                                               \
	-no-undefined                                                         \
	-export-symbols-regex                                                 \
	 '^storaged_.*|g_io_module_load|g_io_module_unload|g_io_module_query' \
	$(NULL)

libstoraged_bcache_la_LIBADD =                                                \
	$(GLIB_LIBS)                                                          \
	$(GIO_LIBS)                                                           \
	$(GUDEV_LIBS)                                                         \
	$(POLKIT_GOBJECT_1_LIBS)                                              \
	$(BCACHE_LIBS)                                                        \
	$(NULL)

# ------------------------------------------------------------------------------

CLEANFILES = storaged-generated-doc-*.xml storaged-generated.[ch]

EXTRA_DIST =                                                                  \
	$(NULL)

include ../Makefile.uninstalled

all-local: module_link

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
