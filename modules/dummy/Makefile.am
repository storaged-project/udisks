## Process this file with automake to produce Makefile.in

SUBDIRS = data

DUMMY_MODULE_DIR = $(top_srcdir)/modules/dummy

MODULE_SO = libstoraged_dummy.so

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
	$(WARN_CFLAGS)                                                         \
	$(NULL)

$(dbus_built_sources): $(DUMMY_MODULE_DIR)/data/org.storaged.Storaged.dummy.xml Makefile.am
	gdbus-codegen                                                          \
	        --interface-prefix org.storaged.Storaged.                      \
	        --c-namespace Dummy                                            \
	        --generate-c-code dummy-generated                              \
	        $<                                                             \
	$(NULL)


module_flags =                                                                 \
	 -export_dynamic                                                       \
	 -avoid-version                                                        \
	 -module                                                               \
	 -no-undefined                                                         \
	 -export-symbols-regex                                                 \
	  '^storaged_module_.*|g_io_module_load|g_io_module_unload|g_io_module_query'

dbus_built_sources =                                                           \
	dummy-generated.h    dummy-generated.c                                 \
	$(NULL)

BUILT_SOURCES =                                                                \
	$(dbus_built_sources)                                                  \
	$(NULL)

libstoraged_dummydir=$(libdir)/storaged/modules
libstoraged_dummy_LTLIBRARIES=libstoraged_dummy.la

libstoraged_dummyincludedir=$(includedir)/storaged/storaged

libstoraged_dummyinclude_HEADERS=                                              \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(NULL)

libstoraged_dummy_la_SOURCES =                                                 \
	$(BUILT_SOURCES)                                                       \
	$(top_srcdir)/modules/storagedmoduleiface.h                            \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                       \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(top_srcdir)/modules/storagedmoduleobject.c                           \
	dummytypes.h                                                           \
	dummymoduleiface.c                                                     \
	dummylinuxblock.h    dummylinuxblock.c                                 \
	dummylinuxdrive.h    dummylinuxdrive.c                                 \
	dummyloopobject.h    dummyloopobject.c                                 \
	dummylinuxloop.h     dummylinuxloop.c                                  \
	dummylinuxmanager.h  dummylinuxmanager.c                               \
	$(NULL)

libstoraged_dummy_la_CPPFLAGS =                                                \
	$(CPPFLAGS)                                                            \
	-DG_LOG_DOMAIN=\"libstoraged-dummy\"                                   \
	$(NULL)

libstoraged_dummy_la_CFLAGS =                                                  \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(NULL)

libstoraged_dummy_la_LDFLAGS = $(module_flags)

libstoraged_dummy_la_LIBADD =                                                  \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(NULL)

# ------------------------------------------------------------------------------

CLEANFILES = dummy-generated.[ch]

EXTRA_DIST =                                                                   \
	$(NULL)

include ../Makefile.uninstalled

all-local: module_link

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
