## Process this file with automake to produce Makefile.in

SUBDIRS = data

ISCSI_MODULE_DIR = $(top_srcdir)/modules/iscsi

MODULE_SO = libudisks2_iscsi.so

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
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

$(dbus_built_sources): $(ISCSI_MODULE_DIR)/data/org.freedesktop.UDisks2.iscsi.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen                                              \
	        --interface-prefix org.freedesktop.UDisks2.                    \
	        --c-namespace UDisks                                           \
	        --c-generate-autocleanup all                                   \
	        --generate-c-code udisks-iscsi-generated                       \
	        --generate-docbook udisks-generated-doc                        \
	        $<

dbus_built_sources =                                                           \
	udisks-iscsi-generated.h        udisks-iscsi-generated.c               \
	$(NULL)

BUILT_SOURCES =                                                                \
	$(dbus_built_sources)                                                  \
	$(NULL)

libudisks2_iscsidir=$(libdir)/udisks2/modules
libudisks2_iscsi_LTLIBRARIES=libudisks2_iscsi.la

libudisks2_iscsiincludedir=$(includedir)/udisks2/udisks

libudisks2_iscsiinclude_HEADERS =                                              \
	$(top_srcdir)/modules/udisksmoduleiface.h                              \
	$(top_srcdir)/modules/udisksmoduleifacetypes.h                         \
	$(top_srcdir)/modules/udisksmoduleobject.h                             \
	$(NULL)

libudisks2_iscsi_la_SOURCES =                                                  \
	$(BUILT_SOURCES)                                                       \
	$(top_srcdir)/modules/udisksmoduleiface.h                              \
	$(top_srcdir)/modules/udisksmoduleifacetypes.h                         \
	$(top_srcdir)/modules/udisksmoduleobject.h                             \
	$(top_srcdir)/modules/udisksmoduleobject.c                             \
	udiskslinuxmanageriscsiinitiator.h                                     \
	udiskslinuxmanageriscsiinitiator.c                                     \
	udisksiscsistate.h                                                     \
	udisksiscsistate.c                                                     \
	udisksiscsitypes.h                                                     \
	udisksiscsimoduleiface.c                                               \
	udisksiscsiutil.h                                                      \
	udisksiscsiutil.c                                                      \
	udisksiscsidbusutil.h                                                  \
	udisksiscsidbusutil.c                                                  \
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
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GUDEV_LIBS)                                                          \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(ISCSI_LIBS)                                                          \
	$(NULL)

# ------------------------------------------------------------------------------

CLEANFILES = udisks-generated-doc-*.xml udisks-generated.[ch]

EXTRA_DIST =                                                                   \
	$(NULL)

include ../Makefile.uninstalled

all-local: module_link

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
