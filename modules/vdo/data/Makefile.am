
NULL =

polkitdir        = $(datadir)/polkit-1/actions
polkit_in_files  = org.freedesktop.UDisks2.vdo.policy.in
polkit_DATA      = $(polkit_in_files:.policy.in=.policy)

@INTLTOOL_POLICY_RULE@

EXTRA_DIST =                                                                   \
	org.freedesktop.UDisks2.vdo.xml                                        \
	$(polkit_in_files)                                                     \
	$(NULL)

clean-local:
	rm -f *~ $(polkit_DATA)
