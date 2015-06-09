## Process this file with automake to produce Makefile.in

SUBDIRS =

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
	-DSTORAGED_COMPILATION                                                 \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

$(dbus_built_sources): Makefile.am $(top_srcdir)/data/org.storaged.Storaged.xml
	gdbus-codegen                                                          \
	        --interface-prefix org.storaged.Storaged.                      \
	        --c-namespace Storaged                                         \
	        --c-generate-object-manager                                    \
	        --generate-c-code storaged-generated                           \
	        --generate-docbook storaged-generated-doc                      \
		$(top_srcdir)/data/org.storaged.Storaged.xml                   \
	$(NULL)

storagedenumtypes.h: storagedenums.h storagedenumtypes.h.template
	( top_builddir=`cd $(top_builddir) && pwd`;                                                \
	  cd $(srcdir) && glib-mkenums --template storagedenumtypes.h.template storagedenums.h ) | \
	          sed 's,U_TYPE_DISKS,STORAGED_TYPE,' | sed 's,u_disks,storaged,' >                \
	          storagedenumtypes.h.tmp && mv storagedenumtypes.h.tmp storagedenumtypes.h

storagedenumtypes.c: storagedenums.h storagedenumtypes.c.template
	( top_builddir=`cd $(top_builddir) && pwd`;                                                \
	  cd $(srcdir) && glib-mkenums --template storagedenumtypes.c.template storagedenums.h ) | \
	          sed 's,U_TYPE_DISKS,STORAGED_TYPE,' | sed 's,u_disks,storaged,' >                \
	          storagedenumtypes.c.tmp && mv storagedenumtypes.c.tmp storagedenumtypes.c

enum_built_sources =                                                           \
	storagedenumtypes.h     storagedenumtypes.c                            \
	$(NULL)

dbus_built_sources =                                                           \
	storaged-generated.h    storaged-generated.c                           \
	$(NULL)

storagedversion.h: $(top_srcdir)/storaged/storagedversion.h.in
	$(SED) -e 's/@MAJOR_VERSION@/$(STORAGED_MAJOR_VERSION)/'               \
	       -e 's/@MINOR_VERSION@/$(STORAGED_MINOR_VERSION)/'               \
	       -e 's/@MICRO_VERSION@/$(STORAGED_MICRO_VERSION)/'               \
	       $(top_srcdir)/storaged/storagedversion.h.in                     \
	       > storagedversion.h.tmp &&                                      \
	       mv storagedversion.h.tmp storagedversion.h

BUILT_SOURCES =                                                                \
	$(enum_built_sources)                                                  \
	$(dbus_built_sources)                                                  \
	storagedversion.h                                                      \
	$(NULL)

lib_LTLIBRARIES=libstoraged.la

libstoragedincludedir=$(includedir)/storaged/storaged

libstoragedinclude_HEADERS=                                                    \
	storaged.h                                                             \
	storagedclient.h                                                       \
	storagedobjectinfo.h                                                   \
	storagedenums.h                                                        \
	storagedenumtypes.h                                                    \
	storagederror.h                                                        \
	storagedtypes.h                                                        \
	storaged-generated.h                                                   \
	storagedversion.h                                                      \
	$(NULL)

libstoraged_la_SOURCES =                                                       \
	$(BUILT_SOURCES)                                                       \
	storagedclient.h        storagedclient.c                               \
	storagedobjectinfo.h    storagedobjectinfo.c                           \
	storagedenums.h                                                        \
	storagederror.h         storagederror.c                                \
	storagedtypes.h                                                        \
	$(NULL)

libstoraged_la_CPPFLAGS =                                                      \
	-DG_LOG_DOMAIN=\"libstoraged\"                                         \
	$(AM_CPPFLAGS)                                                         \
	$(NULL)

libstoraged_la_CFLAGS =                                                        \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(NULL)

libstoraged_la_LIBADD =                                                        \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(NULL)

# ------------------------------------------------------------------------------

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = storaged.pc

# ------------------------------------------------------------------------------

if HAVE_INTROSPECTION

INTROSPECTION_GIRS = Storaged-2.0.gir

girdir = $(INTROSPECTION_GIRDIR)
gir_DATA = Storaged-2.0.gir

typelibsdir = $(INTROSPECTION_TYPELIBDIR)
typelibs_DATA = Storaged-2.0.typelib

Storaged_2_0_gir_SCANNERFLAGS =                                                \
	--c-include='storaged/storaged.h'                                      \
	--warn-all                                                             \
	--namespace Storaged                                                   \
	--identifier-prefix Storaged                                           \
	--symbol-prefix storaged                                               \
	--warn-all                                                             \
	$(NULL)

Storaged_2_0_gir_CFLAGS =                                                      \
	$(libstoraged_la_CFLAGS)                                               \
	-DSTORAGED_COMPILATION                                                 \
	-I$(top_srcdir)                                                        \
	-I$(top_builddir)                                                      \
	$(NULL)

Storaged-2.0.gir: libstoraged.la
Storaged_2_0_gir_INCLUDES = Gio-2.0
Storaged_2_0_gir_LIBS = libstoraged.la
Storaged_2_0_gir_FILES = $(libstoraged_la_SOURCES)
Storaged_2_0_gir_EXPORT_PACKAGES = storaged

include $(INTROSPECTION_MAKEFILE)

endif # HAVE_INTROSPECTION

# ------------------------------------------------------------------------------

CLEANFILES =                                                                   \
	storaged-generated-doc-*.xml                                           \
	storaged-generated.[ch]                                                \
	$(gir_DATA)                                                            \
	$(typelibs_DATA)

EXTRA_DIST =                                                                   \
	storaged.pc.in                                                         \
	storagedversion.h.in                                                   \
	storagedenumtypes.h.template                                           \
	storagedenumtypes.c.template                                           \
	$(NULL)

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local:
	rm -f *~ $(BUILT_SOURCES)
