## Process this file with automake to produce Makefile.in

SUBDIRS = data

MODULE_SO = libstoraged_multipath.so

NULL =

INCLUDES = \
	-I$(top_builddir) -I$(top_srcdir) \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT \
	-DSTORAGED_COMPILATION \
	$(POLKIT_GOBJECT_1_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GUDEV_CFLAGS) \
	$(WARN_CFLAGS) \
	$(NULL)

$(dbus_built_sources): data/org.storaged.Storaged.Multipath.xml Makefile.am
	gdbus-codegen \
		--interface-prefix org.storaged.Storaged. \
		--c-namespace Storaged \
		--generate-c-code mp_generated \
		--generate-docbook doc_generated \
		$<
	$(NULL)

module_flags = \
	-export_dynamic \
	-avoid-version \
	-module \
	-no-undefined \
	-export-symbols-regex \
	'^storaged_module_.*|g_io_module_load|g_io_module_unload|g_io_module_query'

dbus_built_sources = \
	mp_generated.h    mp_generated.c \
	$(NULL)

BUILT_SOURCES = \
	$(dbus_built_sources) \
	$(NULL)

libstoraged_multipathdir=$(libdir)/storaged/modules
libstoraged_multipath_LTLIBRARIES=libstoraged_multipath.la

libstoraged_multipathincludedir=$(includedir)/storaged/storaged

libstoraged_multipathinclude_HEADERS= \
	$(top_srcdir)/modules/storagedmoduleiface.h \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h \
	$(top_srcdir)/modules/storagedmoduleobject.h \
	$(NULL)

libstoraged_multipath_la_SOURCES = \
	$(BUILT_SOURCES) \
	$(top_srcdir)/modules/storagedmoduleiface.h \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h \
	$(top_srcdir)/modules/storagedmoduleobject.h \
	$(top_srcdir)/modules/storagedmoduleobject.c \
	mp_types.h \
	std_lx_mp.c std_lx_mp_obj.c \
	std_lx_mp_pg.c std_lx_mp_pg_obj.c \
	std_lx_mp_path.c std_lx_mp_path_obj.c \
	std_lx_drv_mp.c \
	std_lx_blk_mp.c \
	std_lx_mgr_mp.c \
	mp_module_iface.c \
	$(NULL)

libstoraged_multipath_la_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"libstoraged-mp\" \
	$(NULL)

libstoraged_multipath_la_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GUDEV_CFLAGS) \
	$(POLKIT_GOBJECT_1_CFLAGS) \
	$(LIBDMMP_CFLAGS) \
	$(NULL)

libstoraged_multipath_la_LDFLAGS = $(module_flags)

libstoraged_multipath_la_LIBADD = \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GUDEV_LIBS) \
	$(POLKIT_GOBJECT_1_LIBS) \
	$(LIBDMMP_LIBS) \
	$(NULL)

# ------------------------------------------------------------------------------

CLEANFILES = mp_generated.[ch] doc_generated-*

EXTRA_DIST = \
	$(NULL)

include ../Makefile.uninstalled

all-local: module_link

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ \
		$(BUILT_SOURCES)
