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
	$(DEVKIT_CFLAGS) \
	@GLIB_CFLAGS@

BUILT_SOURCES =						\
	devkit-disks-daemon-glue.h			\
	devkit-disks-device-glue.h

devkit-disks-daemon-glue.h: org.freedesktop.DeviceKit.Disks.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-server --output=devkit-disks-daemon-glue.h org.freedesktop.DeviceKit.Disks.xml

devkit-disks-device-glue.h: org.freedesktop.DeviceKit.Disks.Device.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_device --mode=glib-server --output=devkit-disks-device-glue.h org.freedesktop.DeviceKit.Disks.Device.xml

libexec_PROGRAMS = devkit-disks-daemon

dbusifdir = $(datadir)/dbus-1/interfaces
dbusif_DATA = org.freedesktop.DeviceKit.Disks.xml org.freedesktop.DeviceKit.Disks.Device.xml


# TODO: move to udev
udevhelper_PROGRAMS = part_id
udevhelperdir = /lib/udev

part_id_SOURCES = part-id.c partutil.c partutil.h
part_id_CPPFLAGS = $(AM_CPPFLAGS)
part_id_LDADD = $(GLIB_LIBS)
# end move to udev

devkit_disks_daemon_SOURCES = 						\
	devkit-disks-daemon.h		devkit-disks-daemon.c		\
	devkit-disks-device.h		devkit-disks-device.c		\
	main.c								\
	$(BUILT_SOURCES)

devkit_disks_daemon_CPPFLAGS = 				\
	-I$(top_srcdir)/src				\
	-DG_LOG_DOMAIN=\"devkit-disks-daemon\"		\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

devkit_disks_daemon_LDADD = 				\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)				\
	$(DEVKIT_LIBS)

servicedir       = $(datadir)/dbus-1/system-services
service_in_files = org.freedesktop.DeviceKit.Disks.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dbusconf_in_files = org.freedesktop.DeviceKit.Disks.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

$(dbusconf_DATA): $(dbusconf_in_files) Makefile
	cp $< $@

udevrulesdir = $(sysconfdir)/udev/rules.d
udevrules_DATA = 95-devkit-disks.rules

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = org.freedesktop.DeviceKit.Disks.xml        \
	     org.freedesktop.DeviceKit.Disks.Device.xml \
	     95-devkit-disks.rules                      \
	     $(service_in_files)                        \
	     $(dbusconf_in_files)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)
