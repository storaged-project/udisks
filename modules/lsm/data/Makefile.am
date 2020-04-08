
NULL =

polkitdir        = $(datadir)/polkit-1/actions
polkit_in_files  = org.freedesktop.UDisks2.lsm.policy.in
polkit_DATA      = $(polkit_in_files:.policy.in=.policy)

$(polkit_DATA): $(polkit_in_files)
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

modulesconfdir = $(sysconfdir)/udisks2/modules.conf.d
modulesconf_DATA = udisks2_lsm.conf

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = udisks2-lsm.pc

EXTRA_DIST = \
	org.freedesktop.UDisks2.lsm.xml \
	udisks2_lsm.conf \
	udisks2-lsm.pc.in \
	$(polkit_in_files) \
	$(NULL)

clean-local:
	rm -f *~ $(polkit_DATA)
