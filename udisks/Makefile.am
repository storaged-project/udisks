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

generated-bindings.stamp : Makefile.am $(top_srcdir)/data/org.freedesktop.UDisks.xml
	gdbus-codegen                                                      		\
		--strip-prefix org.freedesktop.UDisks.                          	\
		--namespace UDisks							\
		--output-prefix generated                                       	\
		--include-dir udisks							\
		$(top_srcdir)/data/org.freedesktop.UDisks.xml         			\
		$(NULL)

udisksenumtypes.h: udisksenums.h udisksenumtypes.h.template
	( top_builddir=`cd $(top_builddir) && pwd`; \
	 cd $(srcdir) && glib-mkenums --template udisksenumtypes.h.template udisksenums.h ) | \
	   sed 's,U_TYPE_DISKS,UDISKS_TYPE,' | sed 's,u_disks,udisks,' > \
	   udisksenumtypes.h.tmp && mv udisksenumtypes.h.tmp udisksenumtypes.h

udisksenumtypes.c: udisksenums.h udisksenumtypes.c.template
	( top_builddir=`cd $(top_builddir) && pwd`; \
	 cd $(srcdir) && glib-mkenums --template udisksenumtypes.c.template udisksenums.h ) | \
	   sed 's,U_TYPE_DISKS,UDISKS_TYPE,' | sed 's,u_disks,udisks,' > \
	   udisksenumtypes.c.tmp && mv udisksenumtypes.c.tmp udisksenumtypes.c

BUILT_SOURCES =										\
	generated-bindings.stamp							\
	generated-bindings-sections.txt							\
	generated-bindings.types							\
	generated-bindings.h								\
	generated-bindingsprivate.h		generated-bindingsprivate.c		\
	generated-marshallers.list							\
	generated-marshallers.h			generated-marshallers.c			\
	generated-types.h								\
	generated-udisksblockdevice.h		generated-udisksblockdevice.c		\
	generated-udisksblockdeviceprobed.h	generated-udisksblockdeviceprobed.c	\
	generated-udiskslinuxsysfsdevice.h	generated-udiskslinuxsysfsdevice.c	\
	generated-udisksfilesystem.h		generated-udisksfilesystem.c		\
	generated-udisksjob.h			generated-udisksjob.c			\
	generated-udisksproxymanager.h		generated-udisksproxymanager.c		\
	udisksenumtypes.h			udisksenumtypes.c			\
	$(NULL)

lib_LTLIBRARIES=libudisks.la

libudisksincludedir=$(includedir)/udisks-1.2/udisks

libudisksinclude_HEADERS=								\
	udisks.h									\
	udisksenums.h									\
	udisksenumtypes.h								\
	udiskserror.h									\
	generated-bindings.h								\
	generated-udiskslinuxsysfsdevice.h						\
	generated-udisksblockdevice.h							\
	generated-udisksblockdeviceprobed.h						\
	generated-udisksfilesystem.h							\
	generated-udisksjob.h								\
	$(NULL)

libudisks_la_SOURCES =									\
	$(BUILT_SOURCES)								\
	udisksenums.h									\
	udiskserror.h				udiskserror.c				\
	$(NULL)

libudisks_la_CPPFLAGS = 				\
	-DG_LOG_DOMAIN=\"libudisks\"			\
	$(NULL)

libudisks_la_CFLAGS = 					\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(NULL)

libudisks_la_LIBADD = 					\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

CLEANFILES =

EXTRA_DIST = 						\
	$(NULL)

clean-local :
	rm -f *~ $(BUILT_SOURCES)
