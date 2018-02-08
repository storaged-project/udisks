## Process this file with automake to produce Makefile.in

SUBDIRS = modules.conf.d

NULL =

moduleconfdir = $(sysconfdir)/udisks2/

moduleconf_DATA = udisks2.conf

AM_CPPFLAGS = \
	-I$(top_builddir) -I$(top_srcdir)	 		\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	-DUDISKS_COMPILATION					\
	$(GLIB_CFLAGS) 						\
	$(GIO_CFLAGS)						\
	$(WARN_CFLAGS)						\
	$(NULL)

$(dbus_built_sources) : Makefile.am $(top_srcdir)/data/org.freedesktop.UDisks2.xml
	gdbus-codegen							                \
		--interface-prefix org.freedesktop.UDisks2.                          	\
		--c-namespace UDisks							\
		--c-generate-object-manager						\
		--c-generate-autocleanup all						\
		--generate-c-code udisks-generated                             		\
		--generate-docbook udisks-generated-doc					\
		$(top_srcdir)/data/org.freedesktop.UDisks2.xml         			\
		$(NULL)

udisksenumtypes.h: udisksenums.h udisksenumtypes.h.template
	( top_builddir=`cd $(top_builddir) && pwd`; 						\
	 cd $(srcdir) && glib-mkenums --template udisksenumtypes.h.template udisksenums.h ) | 	\
	   sed 's,U_TYPE_DISKS,UDISKS_TYPE,' | sed 's,u_disks,udisks,' > 			\
	   udisksenumtypes.h.tmp && mv udisksenumtypes.h.tmp udisksenumtypes.h

udisksenumtypes.c: udisksenums.h udisksenumtypes.c.template
	( top_builddir=`cd $(top_builddir) && pwd`; 						\
	 cd $(srcdir) && glib-mkenums --template udisksenumtypes.c.template udisksenums.h ) | 	\
	   sed 's,U_TYPE_DISKS,UDISKS_TYPE,' | sed 's,u_disks,udisks,' > 			\
	   udisksenumtypes.c.tmp && mv udisksenumtypes.c.tmp udisksenumtypes.c

enum_built_sources =									\
	udisksenumtypes.h			udisksenumtypes.c			\
	$(NULL)

dbus_built_sources =									\
	udisks-generated.h			udisks-generated.c			\
	$(NULL)

udisksversion.h: $(top_srcdir)/udisks/udisksversion.h.in
	   sed -e 's/@MAJOR_VERSION@/$(UDISKS_MAJOR_VERSION)/' -e 's/@MINOR_VERSION@/$(UDISKS_MINOR_VERSION)/' -e 's/@MICRO_VERSION@/$(UDISKS_MICRO_VERSION)/' $(top_srcdir)/udisks/udisksversion.h.in > udisksversion.h.tmp && mv udisksversion.h.tmp udisksversion.h

BUILT_SOURCES =										\
	$(enum_built_sources)								\
	$(dbus_built_sources)								\
	udisksversion.h									\
	$(NULL)

lib_LTLIBRARIES=libudisks2.la

libudisks2includedir=$(includedir)/udisks2/udisks

libudisks2include_HEADERS=								\
	udisks.h									\
	udisksclient.h									\
	udisksobjectinfo.h								\
	udisksenums.h									\
	udisksenumtypes.h								\
	udiskserror.h									\
	udiskstypes.h									\
	udisks-generated.h								\
	udisksversion.h									\
	$(NULL)

libudisks2_la_SOURCES =									\
	$(BUILT_SOURCES)								\
	udisksclient.h				udisksclient.c				\
	udisksobjectinfo.h			udisksobjectinfo.c			\
	udisksenums.h									\
	udiskserror.h				udiskserror.c				\
	udiskstypes.h									\
	$(NULL)

libudisks2_la_CPPFLAGS = 				\
	-DG_LOG_DOMAIN=\"libudisks2\"			\
	$(AM_CPPFLAGS)					\
	$(NULL)

libudisks2_la_CFLAGS = 					\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(NULL)

libudisks2_la_LIBADD = 					\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = udisks2.pc

# ----------------------------------------------------------------------------------------------------

if HAVE_INTROSPECTION

INTROSPECTION_GIRS = UDisks-2.0.gir

girdir = $(INTROSPECTION_GIRDIR)
gir_DATA = UDisks-2.0.gir

typelibsdir = $(INTROSPECTION_TYPELIBDIR)
typelibs_DATA = UDisks-2.0.typelib

UDisks_2_0_gir_SCANNERFLAGS = 			\
	--c-include='udisks/udisks.h' 		\
	--warn-all                              \
        --namespace UDisks                      \
        --identifier-prefix UDisks              \
        --symbol-prefix udisks                  \
	--warn-all				\
	$(NULL)

UDisks_2_0_gir_CFLAGS = 			\
        $(libudisks2_la_CFLAGS)        		\
        -DUDISKS_COMPILATION                   	\
	-I$(top_srcdir)				\
	-I$(top_builddir)			\
	$(NULL)

UDisks-2.0.gir: libudisks2.la
UDisks_2_0_gir_INCLUDES = Gio-2.0
UDisks_2_0_gir_LIBS = libudisks2.la
UDisks_2_0_gir_FILES = $(libudisks2_la_SOURCES)
UDisks_2_0_gir_EXPORT_PACKAGES = udisks2

include $(INTROSPECTION_MAKEFILE)

endif # HAVE_INTROSPECTION

# ----------------------------------------------------------------------------------------------------

CLEANFILES = udisks-generated-doc-*.xml udisks-generated.[ch] $(gir_DATA) $(typelibs_DATA)

EXTRA_DIST = 						\
	udisks2.conf.in                                 \
	udisks2.pc.in					\
	udisksversion.h.in				\
	udisksenumtypes.h.template			\
	udisksenumtypes.c.template			\
	$(NULL)

dist-hook :
	(for i in $(BUILT_SOURCES) ; do rm -f $(distdir)/$$i ; done)

clean-local :
	rm -f *~ $(BUILT_SOURCES)
