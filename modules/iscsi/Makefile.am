## Process this file with automake to produce Makefile.in

SUBDIRS = data

MODULE_SO = libstoraged_iscsi.so

NULL =

INCLUDES =                                                                     \
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

$(dbus_built_sources): Makefile.am data/org.storaged.Storaged.iscsi.xml
	gdbus-codegen                                                          \
	        --interface-prefix org.storaged.Storaged.                      \
	        --c-namespace Storaged                                         \
	        --generate-c-code storaged-iscsi-generated                     \
	        --generate-docbook storaged-generated-doc                      \
	        data/org.storaged.Storaged.iscsi.xml                           \
	$(NULL)

dbus_built_sources =                                                           \
	storaged-iscsi-generated.h        storaged-iscsi-generated.c           \
	$(NULL)

BUILT_SOURCES =                                                                \
	$(dbus_built_sources)                                                  \
	$(NULL)

libstoraged_iscsidir=$(libdir)/storaged/modules
libstoraged_iscsi_LTLIBRARIES=libstoraged_iscsi.la

libstoraged_iscsiincludedir=$(includedir)/storaged/storaged

libstoraged_iscsiinclude_HEADERS =                                             \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(NULL)

libstoraged_iscsi_la_SOURCES =                                                 \
	$(BUILT_SOURCES)                                                       \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(top_srcdir)/modules/storagedmoduleobject.c                           \
	storagedlinuxmanageriscsiinitiator.h                                   \
	storagedlinuxmanageriscsiinitiator.c                                   \
	storagediscsistate.h                                                   \
	storagediscsistate.c                                                   \
	storagediscsimoduleiface.c                                             \
	$(NULL)

libstoraged_iscsi_la_CPPFLAGS =                                                \
	-DG_LOG_DOMAIN=\"libstoraged-iscsi\"                                   \
	$(NULL)

libstoraged_iscsi_la_CFLAGS =                                                  \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(NULL)

libstoraged_iscsi_la_LDFLAGS =                                                 \
	-export_dynamic                                                        \
	-avoid-version                                                         \
	-module                                                                \
	-no-undefined                                                          \
	-export-symbols-regex                                                  \
	 '^storaged_.*|g_io_module_load|g_io_module_unload|g_io_module_query'  \
	$(NULL)

libstoraged_iscsi_la_LIBADD =                                                  \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(ISCSI_LIBS)                                                          \
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
