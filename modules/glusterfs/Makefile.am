## Process this file with automake to produce Makefile.in

SUBDIRS = data

GLUSTERFS_MODULE_DIR = $(top_srcdir)/modules/glusterfs

MODULE_SO = libstoraged_glusterfs.so

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
	-DSTORAGED_COMPILATION                                                 \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
        $(LIBXML_CFLAGS)                                                       \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

$(dbus_built_sources): $(GLUSTERFS_MODULE_DIR)/data/org.storaged.Storaged.glusterfs.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen                                              \
	        --interface-prefix org.storaged.Storaged.                      \
	        --c-namespace Storaged                                         \
	        --generate-c-code storaged-glusterfs-generated                 \
	        --generate-docbook storaged-generated-doc                      \
	        $<

dbus_built_sources =                                                           \
	storaged-glusterfs-generated.h        storaged-glusterfs-generated.c   \
	$(NULL)

BUILT_SOURCES =                                                                \
	$(dbus_built_sources)                                                  \
	$(NULL)

libstoraged_glusterfsdir=$(libdir)/storaged/modules
libstoraged_glusterfs_LTLIBRARIES=libstoraged_glusterfs.la

libstoraged_glusterfsincludedir=$(includedir)/storaged/storaged

libstoraged_glusterfsinclude_HEADERS =                                         \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(NULL)

libstoraged_glusterfs_la_SOURCES =                                             \
	$(BUILT_SOURCES)                                                       \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(top_srcdir)/modules/storagedmoduleobject.c                           \
	storagedglusterfstypes.h                                               \
	storagedglusterfsmoduleiface.c                                         \
	storagedlinuxmanagerglusterfs.h                                        \
        storagedlinuxmanagerglusterfs.c                                        \
	storagedlinuxglusterfsvolumeobject.h                                   \
	storagedlinuxglusterfsvolumeobject.c                                   \
	storagedlinuxglusterfsbrick.h                                          \
	storagedlinuxglusterfsbrick.c                                          \
	storagedlinuxglusterfsbrickobject.h                                    \
	storagedlinuxglusterfsbrickobject.c                                    \
        storagedlinuxglusterfsglusterdobject.h                                 \
        storagedlinuxglusterfsglusterdobject.c                                 \
        storagedlinuxglusterfsglusterd.h                                       \
        storagedlinuxglusterfsglusterd.c                                       \
        storagedlinuxglusterfsvolume.h          storagedlinuxglusterfsvolume.c \
	storagedglusterfsutils.h                storagedglusterfsutils.c       \
        storagedglusterfsinfo.h                 storagedglusterfsinfo.c        \
	storagedglusterfsstate.h                storagedglusterfsstate.c       \
	$(NULL)

libstoraged_glusterfs_la_CPPFLAGS =                                            \
	$(CPPFLAGS)                                                            \
	-DG_LOG_DOMAIN=\"libstoraged-glusterfs\"                               \
	$(NULL)

libstoraged_glusterfs_la_CFLAGS =                                              \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(NULL)

libstoraged_glusterfs_la_LDFLAGS =                                             \
	-export_dynamic                                                        \
	-avoid-version                                                         \
	-module                                                                \
	-no-undefined                                                          \
	-export-symbols-regex                                                  \
	 '^storaged_.*|g_io_module_load|g_io_module_unload|g_io_module_query'  \
	$(NULL)

libstoraged_glusterfs_la_LIBADD =                                              \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(LIBXML_LIBS)                                                         \
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
