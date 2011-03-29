
NULL =

servicedir       = $(datadir)/dbus-1/system-services
service_in_files = org.freedesktop.UDisks2.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@privlibdir\@|$(privlibdir)|" $< > $@

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dbusconf_in_files = org.freedesktop.UDisks2.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

$(dbusconf_DATA): $(dbusconf_in_files) Makefile
	cp $< $@

udevrulesdir = $(slashlibdir)/udev/rules.d
udevrules_DATA = 80-udisks2.rules

EXTRA_DIST = 						\
	80-udisks2.rules				\
	org.freedesktop.UDisks2.xml			\
	$(service_in_files)				\
	$(dbusconf_in_files)				\
	$(NULL)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)
