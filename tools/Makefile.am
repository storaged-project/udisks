## Process this file with automake to produce Makefile.in

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
	$(POLKIT_DBUS_CFLAGS) \
	$(GLIB_CFLAGS)

BUILT_SOURCES =							\
	udisks-daemon-glue.h					\
	udisks-device-glue.h					\
	udisks-marshal.h		udisks-marshal.c

udisks-marshal.h: $(top_srcdir)/src/marshal.list
	glib-genmarshal $< --prefix=udisks_marshal --header > $@

udisks-marshal.c: $(top_srcdir)/src/marshal.list
	echo "#include \"udisks-marshal.h\"" > $@ && glib-genmarshal $< --prefix=udisks_marshal --body >> $@

udisks-daemon-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.xml Makefile.am
	dbus-binding-tool --prefix=udisks_daemon --mode=glib-client --output=udisks-daemon-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.xml

udisks-device-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml Makefile.am
	dbus-binding-tool --prefix=udisks_daemon --mode=glib-client --output=udisks-device-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml

bin_PROGRAMS = udisks

udisks_SOURCES = 					\
				udisks.c		\
	$(BUILT_SOURCES)


udisks_CPPFLAGS = 					\
	-DG_LOG_DOMAIN=\"udisks\"		        \
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

udisks_CFLAGS = 					\
	$(LIBATASMART_CFLAGS)				\
	$(NULL)

udisks_LDADD = 						\
	$(LIBATASMART_LIBS)				\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)

slashsbin_PROGRAMS = umount.udisks

umount_udisks_SOURCES = umount-udisks.c
umount_udisks_LDADD = $(DBUS_GLIB_LIBS) $(POLKIT_DBUS_LIBS)

# ----------------------------------------------------------------------------------------------------

if REMOTE_ACCESS_ENABLED
bin_PROGRAMS += udisks-tcp-bridge
endif # REMOTE_ACCESS_ENABLED

udisks_tcp_bridge_SOURCES = udisks-tcp-bridge.c
udisks_tcp_bridge_LDADD = $(DBUS_GLIB_LIBS)

# ----------------------------------------------------------------------------------------------------

CLEANFILES = $(BUILT_SOURCES)

profiledir = $(sysconfdir)/profile.d
profile_SCRIPTS = udisks-bash-completion.sh

EXTRA_DIST = $(profile_SCRIPTS)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)
