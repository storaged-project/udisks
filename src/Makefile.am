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
	$(GUDEV_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS)

BUILT_SOURCES =						\
	daemon-glue.h					\
	device-glue.h					\
	adapter-glue.h					\
	port-glue.h					\
	expander-glue.h					\
	marshal.h		marshal.c

marshal.h: marshal.list
	glib-genmarshal $< --prefix=marshal --header > $@

marshal.c: marshal.list
	echo "#include \"marshal.h\"" > $@ && glib-genmarshal $< --prefix=marshal --body >> $@

daemon-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.xml Makefile.am
	dbus-binding-tool --prefix=daemon --mode=glib-server --output=daemon-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.xml

device-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml Makefile.am
	dbus-binding-tool --prefix=device --mode=glib-server --output=device-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml

adapter-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Adapter.xml Makefile.am
	dbus-binding-tool --prefix=adapter --mode=glib-server --output=adapter-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Adapter.xml

expander-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Expander.xml Makefile.am
	dbus-binding-tool --prefix=expander --mode=glib-server --output=expander-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Expander.xml

port-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Port.xml Makefile.am
	dbus-binding-tool --prefix=port --mode=glib-server --output=port-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Port.xml

libexec_PROGRAMS = udisks-daemon

udisks_daemon_SOURCES = 						\
	types.h								\
	private.h							\
	profile.h							\
	daemon.h			daemon.c			\
	device.h			device.c			\
	device-private.h		device-private.c		\
	adapter.h			adapter.c			\
	adapter-private.h		adapter-private.c		\
	expander.h			expander.c			\
	expander-private.h		expander-private.c		\
	port.h				port.c				\
	port-private.h			port-private.c			\
	mount-file.h			mount-file.c			\
	mount.h				mount.c				\
	mount-monitor.h			mount-monitor.c			\
	inhibitor.h			inhibitor.c			\
	poller.h			poller.c			\
	main.c								\
	$(BUILT_SOURCES)

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

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 						\
	marshal.list					\
	$(NULL)

install-data-local:
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/lib/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/lib/udisks
