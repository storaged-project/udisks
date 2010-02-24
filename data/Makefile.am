
NULL =

dbusifdir = $(datadir)/dbus-1/interfaces
dbusif_DATA = \
	org.freedesktop.UDisks.xml 		\
	org.freedesktop.UDisks.Device.xml 	\
	org.freedesktop.UDisks.Adapter.xml	\
	org.freedesktop.UDisks.Port.xml	\
	org.freedesktop.UDisks.Expander.xml	\
	$(NULL)

servicedir       = $(datadir)/dbus-1/system-services
service_in_files = org.freedesktop.UDisks.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dbusconf_in_files = org.freedesktop.UDisks.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

$(dbusconf_DATA): $(dbusconf_in_files) Makefile
	cp $< $@

udevrulesdir = $(slashlibdir)/udev/rules.d
udevrules_DATA = 80-udisks.rules

pkgconfigdir = $(datadir)/pkgconfig
pkgconfig_DATA = udisks.pc

if REMOTE_ACCESS_ENABLED
avahiservicedir = $(sysconfdir)/avahi/services
avahiservice_DATA = udisks.service
endif # REMOTE_ACCESS_ENABLED

EXTRA_DIST = 						\
	80-udisks.rules					\
	$(dbusif_DATA)					\
	$(service_in_files)				\
	$(dbusconf_in_files)				\
	udisks.pc.in					\
	udisks.service					\
	$(NULL)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)
