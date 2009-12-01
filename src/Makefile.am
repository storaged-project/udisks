## Process this file with automake to produce Makefile.in

SUBDIRS = helpers probers

NULL =

INCLUDES = \
	-I$(top_builddir)/src -I$(top_srcdir)/src \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT	\
	$(DBUS_GLIB_CFLAGS) \
	$(POLKIT_GOBJECT_1_CFLAGS) \
	$(GUDEV_CFLAGS) -DG_UDEV_API_IS_SUBJECT_TO_CHANGE \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS)

BUILT_SOURCES =							\
	devkit-disks-daemon-glue.h				\
	devkit-disks-device-glue.h				\
	devkit-disks-adapter-glue.h				\
	devkit-disks-port-glue.h				\
	devkit-disks-expander-glue.h				\
	devkit-disks-marshal.h		devkit-disks-marshal.c

devkit-disks-marshal.h: udisks-marshal.list
	glib-genmarshal $< --prefix=devkit_disks_marshal --header > $@

devkit-disks-marshal.c: udisks-marshal.list
	echo "#include \"devkit-disks-marshal.h\"" > $@ && glib-genmarshal $< --prefix=devkit_disks_marshal --body >> $@

devkit-disks-daemon-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-server --output=devkit-disks-daemon-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.xml

devkit-disks-device-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_device --mode=glib-server --output=devkit-disks-device-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml

devkit-disks-adapter-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Adapter.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_adapter --mode=glib-server --output=devkit-disks-adapter-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Adapter.xml

devkit-disks-expander-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Expander.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_expander --mode=glib-server --output=devkit-disks-expander-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Expander.xml

devkit-disks-port-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Port.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_port --mode=glib-server --output=devkit-disks-port-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Port.xml

libexec_PROGRAMS = udisks-daemon

udisks_daemon_SOURCES = 								\
	devkit-disks-types.h								\
	devkit-disks-private.h								\
	devkit-disks-daemon.h			devkit-disks-daemon.c			\
	devkit-disks-device.h			devkit-disks-device.c			\
	devkit-disks-device-private.h		devkit-disks-device-private.c		\
	devkit-disks-adapter.h			devkit-disks-adapter.c			\
	devkit-disks-adapter-private.h		devkit-disks-adapter-private.c		\
	devkit-disks-expander.h			devkit-disks-expander.c			\
	devkit-disks-expander-private.h		devkit-disks-expander-private.c		\
	devkit-disks-port.h			devkit-disks-port.c			\
	devkit-disks-port-private.h		devkit-disks-port-private.c		\
	devkit-disks-mount-file.h		devkit-disks-mount-file.c		\
	devkit-disks-mount.h			devkit-disks-mount.c			\
	devkit-disks-mount-monitor.h		devkit-disks-mount-monitor.c		\
	devkit-disks-inhibitor.h		devkit-disks-inhibitor.c		\
	devkit-disks-poller.h			devkit-disks-poller.c			\
	main.c										\
	$(BUILT_SOURCES)

udisks_daemon_CPPFLAGS = 				\
	-I$(top_srcdir)/src				\
	-DG_LOG_DOMAIN=\"devkit-disks-daemon\"		\
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
	devkit-disks-polkit-action-lookup.c                             \
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
	udisks-marshal.list				\
	$(NULL)

install-data-local:
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/lib/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/lib/udisks
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/run/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/run/udisks
