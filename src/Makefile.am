## Process this file with automake to produce Makefile.in

SUBDIRS = helpers probers

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
	$(DBUS_GLIB_CFLAGS) 					\
	$(POLKIT_GOBJECT_1_CFLAGS) 				\
	$(GUDEV_CFLAGS) 					\
	$(GLIB_CFLAGS) 						\
	$(GIO_CFLAGS)						\
	$(NULL)

generated-bindings.stamp : Makefile.am $(top_srcdir)/data/org.freedesktop.UDisks.xml $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml
	gdbus-binding-tool                                                      \
		--strip-prefix org.freedesktop.UDisks.                          \
		--output-prefix generated                                       \
		$(top_srcdir)/data/org.freedesktop.UDisks.xml         		\
		$(top_srcdir)/data/org.freedesktop.UDisks.Device.xml         	\
		$(NULL)

BUILT_SOURCES =								\
	generated-bindings.stamp					\
	generated-bindings.h						\
	generated-daemon.h		generated-daemon.c		\
	generated-device.h		generated-device.c		\
	generated-marshallers.h		generated-marshallers.c		\
	generated-typemappers.h						\
	$(NULL)

libexec_PROGRAMS = udisks-daemon

udisks_daemon_SOURCES = 						\
	types.h								\
	error.h				error.c				\
					main.c				\
	linuxdaemon.h			linuxdaemon.c			\
	private.h							\
	profile.h							\
	$(BUILT_SOURCES)						\
	$(NULL)

#udisks_daemon_SOURCES = 						\
#	types.h								\
#	private.h							\
#	profile.h							\
#	daemon.h			daemon.c			\
#	device.h			device.c			\
#	device-private.h		device-private.c		\
#	adapter.h			adapter.c			\
#	adapter-private.h		adapter-private.c		\
#	expander.h			expander.c			\
#	expander-private.h		expander-private.c		\
#	port.h				port.c				\
#	port-private.h			port-private.c			\
#	mount-file.h			mount-file.c			\
#	mount.h				mount.c				\
#	mount-monitor.h			mount-monitor.c			\
#	inhibitor.h			inhibitor.c			\
#	poller.h			poller.c			\
#	main.c								\
#	$(BUILT_SOURCES)						\
#	$(NULL)
#

udisks_daemon_CPPFLAGS = 				\
	-I$(top_srcdir)/src				\
	-DG_LOG_DOMAIN=\"udisks-daemon\"		\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)					\
	$(NULL)

udisks_daemon_CFLAGS = 					\
	$(LIBATASMART_CFLAGS)				\
	$(NULL)

udisks_daemon_LDADD = 					\
	$(GIO_LIBS)					\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_GOBJECT_1_LIBS) 			\
	$(GUDEV_LIBS)					\
	$(LIBATASMART_LIBS)				\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

polkitmodulesdir = $(libdir)/polkit-1/extensions
polkitmodules_LTLIBRARIES = libudisks-action-lookup.la

libudisks_action_lookup_la_SOURCES =                    		\
	polkit-action-lookup.c                             		\
        $(NULL)

libudisks_action_lookup_la_CFLAGS =                        		\
        -DPOLKIT_BACKEND_I_KNOW_API_IS_SUBJECT_TO_CHANGE                \
        -DG_LOG_DOMAIN=\"UDisks-Action-Lookup\"            		\
	$(POLKIT_BACKEND_1_CFLAGS) 					\
        $(NULL)

libudisks_action_lookup_la_LDFLAGS =                       		\
	-export_dynamic -avoid-version -module -no-undefined 		\
	-export-symbols-regex '^g_io_module_(load|unload)' 		\
	$(POLKIT_BACKEND_1_LIBS) 					\
        $(NULL)

libudisks_action_lookup_la_LIBADD =                 			\
        $(NULL)

# ----------------------------------------------------------------------------------------------------

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 						\
	marshal.list					\
	$(NULL)

install-data-local:
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/lib/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/lib/udisks
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/run/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/run/udisks
