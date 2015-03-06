## Process this file with automake to produce Makefile.in

SUBDIRS = data

NULL =

INCLUDES = \
	-I$(top_builddir) -I$(top_srcdir)	 		\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-DLVM_HELPER_DIR=\""$(prefix)/lib/storaged/"\" 		\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	-DSTORAGED_COMPILATION					\
	$(POLKIT_GOBJECT_1_CFLAGS)				\
	$(GLIB_CFLAGS) 						\
	$(GIO_CFLAGS)						\
	$(GUDEV_CFLAGS)						\
	$(WARN_CFLAGS)						\
	$(NULL)

$(dbus_built_sources) : Makefile.am data/org.storaged.Storaged.lvm2.xml
	gdbus-codegen									\
		--interface-prefix org.storaged.Storaged.				\
		--c-namespace Storaged							\
		--generate-c-code module-lvm2-generated					\
		data/org.storaged.Storaged.lvm2.xml					\
		$(NULL)


module_flags = -export_dynamic -avoid-version -module -no-undefined -export-symbols-regex '^storaged_module_.*|g_io_module_load|g_io_module_unload|g_io_module_query'

dbus_built_sources =									\
	module-lvm2-generated.h			module-lvm2-generated.c			\
	$(NULL)

BUILT_SOURCES =										\
	$(dbus_built_sources)								\
	$(NULL)

libstoraged_lvm2dir=$(libdir)/storaged/modules
libstoraged_lvm2_LTLIBRARIES=libstoraged_lvm2.la

libstoraged_lvm2includedir=$(includedir)/storaged/storaged

libstoraged_lvm2include_HEADERS=								\
	$(top_srcdir)/modules/storagedmoduleiface.h						\
	$(top_srcdir)/modules/storagedmoduleifacetypes.h					\
	$(top_srcdir)/modules/storagedmoduleobject.h						\
	$(NULL)

libstoraged_lvm2_la_SOURCES =									\
	$(BUILT_SOURCES)									\
	$(top_srcdir)/modules/storagedmoduleiface.h						\
	$(top_srcdir)/modules/storagedmoduleifacetypes.h					\
	$(top_srcdir)/modules/storagedmoduleobject.h						\
	$(top_srcdir)/modules/storagedmoduleobject.c						\
	storagedlvm2types.h									\
	storagedlvm2moduleiface.c								\
	storagedlinuxlogicalvolume.h			storagedlinuxlogicalvolume.c		\
	storagedlinuxlogicalvolumeobject.h		storagedlinuxlogicalvolumeobject.c	\
	storagedlinuxphysicalvolume.h			storagedlinuxphysicalvolume.c		\
	storagedlinuxvolumegroup.h			storagedlinuxvolumegroup.c		\
	storagedlinuxvolumegroupobject.h		storagedlinuxvolumegroupobject.c	\
	storagedlinuxblocklvm2.h			storagedlinuxblocklvm2.c		\
	storagedlvm2daemonutil.h			storagedlvm2daemonutil.c		\
	storagedlvm2dbusutil.h				storagedlvm2dbusutil.c			\
	storagedlinuxmanagerlvm2.h			storagedlinuxmanagerlvm2.c		\
	storagedlvm2state.h				storagedlvm2state.c			\
	$(NULL)

libstoraged_lvm2_la_CPPFLAGS = 				\
	-DG_LOG_DOMAIN=\"libstoraged-lvm2\"		\
	$(NULL)

libstoraged_lvm2_la_CFLAGS = 				\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(GUDEV_CFLAGS)					\
	$(POLKIT_GOBJECT_1_CFLAGS)			\
	$(NULL)

libstoraged_lvm2_la_LDFLAGS = $(module_flags)

libstoraged_lvm2_la_LIBADD = 				\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(GUDEV_LIBS)					\
	$(POLKIT_GOBJECT_1_LIBS)			\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

storageddprivdir = $(prefix)/lib/storaged
storageddpriv_PROGRAMS = storaged-lvm

storaged_lvm_SOURCES = 					\
	storagedlvmhelper.c				\
	$(NULL)

storaged_lvm_CFLAGS = 					\
	-DG_LOG_DOMAIN=\"storaged-lvm-helper\"		\
	$(DEVMAPPER_CFLAGS)				\
	$(LVM2_CFLAGS)					\
	$(NULL)

storaged_lvm_LDADD = 					\
	$(GLIB_LIBS)					\
	$(DEVMAPPER_LIBS)				\
	$(LVM2_LIBS)					\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

CLEANFILES = module-lvm2-generated.[ch]

EXTRA_DIST = 						\
	$(NULL)

dist-hook :
	(for i in $(BUILT_SOURCES) ; do rm -f $(distdir)/$$i ; done)

clean-local :
	rm -f *~ $(BUILT_SOURCES)
