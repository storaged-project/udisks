
NULL =

dbusifdir = $(datadir)/dbus-1/interfaces
dbusif_DATA = org.freedesktop.DeviceKit.Disks.xml org.freedesktop.DeviceKit.Disks.Device.xml org.freedesktop.DeviceKit.Disks.Controller.xml

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

udevrulesdir = $(slashlibdir)/udev/rules.d
udevrules_DATA = 95-devkit-disks.rules

pkgconfigdir = $(datadir)/pkgconfig
pkgconfig_DATA = DeviceKit-disks.pc

EXTRA_DIST = 						\
	org.freedesktop.DeviceKit.Disks.xml		\
	org.freedesktop.DeviceKit.Disks.Device.xml	\
	org.freedesktop.DeviceKit.Disks.Controller.xml	\
	95-devkit-disks.rules				\
	$(service_in_files)				\
	$(dbusconf_in_files)				\
	DeviceKit-disks.pc.in				\
	$(NULL)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)
