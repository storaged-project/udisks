
NULL =

dbusservicedir       = $(datadir)/dbus-1/system-services
dbusservice_in_files = org.storaged.Storaged.service.in
dbusservice_DATA     = $(dbusservice_in_files:.service.in=.service)

$(dbusservice_DATA): $(dbusservice_in_files) Makefile
	@sed -e "s|\@storagedprivdir\@|$(prefix)/lib/storaged|" $< > $@

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dbusconf_in_files = org.storaged.Storaged.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

$(dbusconf_DATA): $(dbusconf_in_files) Makefile
	cp $< $@

systemdservice_in_files = storaged.service.in

if HAVE_SYSTEMD
systemdservicedir       = $(systemdsystemunitdir)
systemdservice_DATA     = $(systemdservice_in_files:.service.in=.service)

$(systemdservice_DATA): $(systemdservice_in_files) Makefile
	@sed -e "s|\@storagedprivdir\@|$(prefix)/lib/storaged|" $< > $@
endif

udevrulesdir = $(udevdir)/rules.d
udevrules_DATA = 80-storaged.rules

polkitdir        = $(datadir)/polkit-1/actions
polkit_in_files  = org.storaged.Storaged.policy.in
polkit_DATA      = $(polkit_in_files:.policy.in=.policy)

completionsdir = $(datadir)/bash-completion/completions
completions_DATA = 					\
	completions/storagedctl				\
	$(NULL)

@INTLTOOL_POLICY_RULE@

EXTRA_DIST = 						\
	80-storaged.rules				\
	org.storaged.Storaged.xml			\
	$(systemdservice_in_files)			\
	$(dbusservice_in_files)				\
	$(dbusconf_in_files)				\
	$(polkit_in_files)				\
	$(completions_DATA)				\
	$(NULL)

clean-local :
	rm -f *~ $(dbusservice_DATA) $(dbusconf_DATA) $(systemdservice_DATA) $(polkit_DATA)
