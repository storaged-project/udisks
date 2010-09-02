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

BUILT_SOURCES =										\
	generated-bindings.stamp							\
	generated-bindings.h								\
	generated-bindingsprivate.h		generated-bindingsprivate.c		\
	generated-marshallers.list							\
	generated-marshallers.h			generated-marshallers.c			\
	generated-types.h								\
	generated-udisksblockdevice.h		generated-udisksblockdevice.c		\
	generated-udisksblockdeviceprobed.h	generated-udisksblockdeviceprobed.c	\
	generated-udiskslinuxsysfsdevice.h	generated-udiskslinuxsysfsdevice.c	\
	$(NULL)

lib_LTLIBRARIES=libudisks.la

libudisksincludedir=$(includedir)/udisks-1.2/udisks
libudisksinclude_HEADERS=
	udisks.h					\
	generated-bindings.h				\
	generated-udiskslinuxsysfsdevice.h		\
	generated-udisksblockdevice.h			\
	generated-udisksblockdeviceprobed.h		\
	$(NULL)

libudisks_la_SOURCES =					\
	$(BUILT_SOURCES)				\
	$(NULL)

libudisks_la_CPPFLAGS = 				\
	-DG_LOG_DOMAIN=\"libudisks\"			\
	$(AM_CPPFLAGS)					\
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

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 						\
	$(NULL)
