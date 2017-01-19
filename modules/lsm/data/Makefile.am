
NULL =

polkitdir        = $(datadir)/polkit-1/actions
polkit_in_files  = org.freedesktop.UDisks2.lsm.policy.in
polkit_DATA      = $(polkit_in_files:.policy.in=.policy)

@INTLTOOL_POLICY_RULE@

modulesconfdir = $(sysconfdir)/udisks2/modules.conf.d
modulesconf_DATA = udisks2_lsm.conf

EXTRA_DIST = \
	org.freedesktop.UDisks2.lsm.xml \
	udisks2_lsm.conf \
	$(polkit_in_files) \
	$(NULL)

clean-local:
	rm -f *~ $(polkit_DATA)
