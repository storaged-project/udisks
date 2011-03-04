## Process this file with automake to produce Makefile.in

SUBDIRS =

NULL =

INCLUDES = \
	-I$(top_builddir)/src -I$(top_srcdir)/src 		\
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
	$(NULL)

$(dbus_built_sources) : Makefile.am $(top_srcdir)/data/org.freedesktop.UDisks2.xml
	gdbus-codegen							                \
		--strip-prefix org.freedesktop.UDisks2.                          	\
		--namespace UDisks							\
		--generate-c-code udisks-generated                             		\
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

BUILT_SOURCES =										\
	$(enum_built_sources)								\
	$(dbus_built_sources)								\
	$(NULL)

lib_LTLIBRARIES=libudisks2.la

libudisks2includedir=$(includedir)/udisks2/udisks

libudisks2include_HEADERS=								\
	udisks.h									\
	udisksenums.h									\
	udisksenumtypes.h								\
	udiskserror.h									\
	udiskstypes.h									\
	udisks-generated.h								\
	$(NULL)

libudisks2_la_SOURCES =									\
	$(BUILT_SOURCES)								\
	udisksenums.h									\
	udiskserror.h				udiskserror.c				\
	udiskstypes.h									\
	$(NULL)

libudisks2_la_CPPFLAGS = 				\
	-DG_LOG_DOMAIN=\"libudisks2\"			\
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

CLEANFILES =

EXTRA_DIST = 						\
	udisks2.pc.in					\
	$(NULL)

clean-local :
	rm -f *~ $(BUILT_SOURCES)
