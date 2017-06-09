## Process this file with automake to produce Makefile.in

SUBDIRS = data

MODULE_SO = libudisks2_multipath.so

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
	-DDISKS_COMPILATION \
	$(POLKIT_GOBJECT_1_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GUDEV_CFLAGS) \
	$(WARN_CFLAGS) \
	$(NULL)

$(dbus_built_sources): data/org.freedesktop.UDisks2.Multipath.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen \
		--interface-prefix org.freedesktop.UDisks2. \
		--c-namespace UDisks \
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
	 '^udisks_.*|g_io_module_load|g_io_module_unload|g_io_module_query'

dbus_built_sources = \
	mp_generated.h    mp_generated.c \
	$(NULL)

BUILT_SOURCES = \
	$(dbus_built_sources) \
	$(NULL)

libudisks2_multipathdir=$(libdir)/udisks2/modules
libudisks2_multipath_LTLIBRARIES=libudisks2_multipath.la

libudisks2_multipathincludedir=$(includedir)/udisks2/udisks

libudisks2_multipathinclude_HEADERS= \
	$(top_srcdir)/modules/udisksmoduleiface.h \
	$(top_srcdir)/modules/udisksmoduleifacetypes.h \
	$(top_srcdir)/modules/udisksmoduleobject.h \
	$(NULL)

libudisks2_multipath_la_SOURCES = \
	$(BUILT_SOURCES) \
	$(top_srcdir)/modules/udisksmoduleiface.h \
	$(top_srcdir)/modules/udisksmoduleifacetypes.h \
	$(top_srcdir)/modules/udisksmoduleobject.h \
	$(top_srcdir)/modules/udisksmoduleobject.c \
	mp_types.h \
	ud_lx_mp.c ud_lx_mp_obj.c \
	ud_lx_mp_path.c ud_lx_mp_path_obj.c \
	ud_lx_drv_mp.c \
	ud_lx_blk_mp.c \
	ud_lx_mgr_mp.c \
	mp_module_iface.c \
	$(NULL)

libudisks2_multipath_la_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"libudisks-mp\" \
	$(NULL)

libudisks2_multipath_la_CFLAGS = \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GUDEV_CFLAGS) \
	$(POLKIT_GOBJECT_1_CFLAGS) \
	$(LIBDMMP_CFLAGS) \
	$(NULL)

libudisks2_multipath_la_LDFLAGS = $(module_flags)

libudisks2_multipath_la_LIBADD = \
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
