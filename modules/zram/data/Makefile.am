
NULL = 

systemdservice_in_files = udisks2-zram-setup@.service.in

if ENABLE_DAEMON

polkitdir       = $(datadir)/polkit-1/actions
polkit_in_files = org.freedesktop.UDisks2.zram.policy.in
polkit_DATA     = $(polkit_in_files:.policy.in=.policy)

$(polkit_DATA): $(polkit_in_files)
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

systemdservicedir   = $(systemdsystemunitdir)
systemdservice_DATA = $(systemdservice_in_files:.service.in=.service)

$(systemdservice_DATA): $(systemdservice_in_files) Makefile
	@sed -e "s|\@zramconfdir\@|$(zramconfdir)|" $< > $@

endif # ENABLE_DAEMON


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = udisks2-zram.pc

EXTRA_DIST =                                                                   \
	org.freedesktop.UDisks2.zram.xml                                       \
	udisks2-zram.pc.in                                                     \
	$(systemdservice_in_files)                                             \
	$(polkit_in_files)                                                     \
	$(NULL)

clean-local:
	rm -f *~ $(polkit_DATA) $(systemdservice_DATA)
