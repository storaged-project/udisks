
NULL =

if ENABLE_DAEMON
SUBDIRS = tmpfiles.d

dbusservicedir       = $(datadir)/dbus-1/system-services
dbusservice_in_files = org.freedesktop.UDisks2.service.in
dbusservice_DATA     = $(dbusservice_in_files:.service.in=.service)

$(dbusservice_DATA): $(dbusservice_in_files) Makefile
	@sed -e "s|\@udisksdprivdir\@|$(libexecdir)/udisks2|" $< > $@

dbusconfdir = $(datadir)/dbus-1/system.d
dbusconf_in_files = org.freedesktop.UDisks2.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

$(dbusconf_DATA): $(dbusconf_in_files) Makefile
	cp $< $@

systemdservice_in_files = udisks2.service.in clean-mount-point@.service

if HAVE_SYSTEMD
systemdservicedir       = $(systemdsystemunitdir)
systemdservice_DATA     = $(systemdservice_in_files:.service.in=.service)

$(systemdservice_DATA): udisks2.service.in Makefile
	@sed -e "s|\@udisksdprivdir\@|$(libexecdir)/udisks2|" $(srcdir)/udisks2.service.in > udisks2.service
endif

udevrulesdir = $(udevdir)/rules.d
udevrules_DATA = 80-udisks2.rules

polkitdir        = $(datadir)/polkit-1/actions
polkit_in_files  = org.freedesktop.UDisks2.policy.in
polkit_DATA      = $(polkit_in_files:.policy.in=.policy)

$(polkit_DATA): $(polkit_in_files)
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@
endif

completionsdir = $(datadir)/bash-completion/completions
completions_DATA =                                                             \
	completions/udisksctl                                                  \
	$(NULL)

EXTRA_DIST =                                                                   \
	80-udisks2.rules                                                       \
	org.freedesktop.UDisks2.xml                                            \
	$(systemdservice_in_files)                                             \
	$(dbusservice_in_files)                                                \
	$(dbusconf_in_files)                                                   \
	$(polkit_in_files)                                                     \
	$(completions_DATA)                                                    \
	$(NULL)

clean-local:
	rm -f *~                                                               \
	udisks2.service                                                        \
	$(dbusservice_DATA)                                                    \
	$(dbusconf_DATA)                                                       \
	$(polkit_DATA)
