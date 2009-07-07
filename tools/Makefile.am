## Process this file with automake to produce Makefile.in

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
	devkit-disks-daemon-glue.h				\
	devkit-disks-device-glue.h				\
	devkit-disks-marshal.h		devkit-disks-marshal.c

devkit-disks-marshal.h: $(top_srcdir)/src/devkit-disks-marshal.list
	glib-genmarshal $< --prefix=devkit_disks_marshal --header > $@

devkit-disks-marshal.c: $(top_srcdir)/src/devkit-disks-marshal.list
	echo "#include \"devkit-disks-marshal.h\"" > $@ && glib-genmarshal $< --prefix=devkit_disks_marshal --body >> $@

devkit-disks-daemon-glue.h: $(top_srcdir)/data/org.freedesktop.DeviceKit.Disks.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-client --output=devkit-disks-daemon-glue.h $(top_srcdir)/data/org.freedesktop.DeviceKit.Disks.xml

devkit-disks-device-glue.h: $(top_srcdir)/data/org.freedesktop.DeviceKit.Disks.Device.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-client --output=devkit-disks-device-glue.h $(top_srcdir)/data/org.freedesktop.DeviceKit.Disks.Device.xml

bin_PROGRAMS = devkit-disks

devkit_disks_SOURCES = 					\
				devkit-disks.c		\
	$(BUILT_SOURCES)


devkit_disks_CPPFLAGS = 				\
	-DG_LOG_DOMAIN=\"devkit-disks\"		        \
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

devkit_disks_LDADD = 					\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)

slashsbin_PROGRAMS = umount.devkit
umount_devkit_SOURCES = umount-devkit.c
umount_devkit_LDADD = $(DBUS_GLIB_LIBS) $(POLKIT_DBUS_LIBS)

CLEANFILES = $(BUILT_SOURCES)

profiledir = $(sysconfdir)/profile.d
profile_SCRIPTS = devkit-disks-bash-completion.sh

EXTRA_DIST = $(profile_SCRIPTS)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)
