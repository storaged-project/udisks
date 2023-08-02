
NULL =

polkit_in_files  = org.freedesktop.UDisks2.iscsi.policy.in

if ENABLE_DAEMON

polkitdir        = $(datadir)/polkit-1/actions
polkit_DATA      = $(polkit_in_files:.policy.in=.policy)

$(polkit_DATA): $(polkit_in_files)
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

endif # ENABLE_DAEMON


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = udisks2-iscsi.pc

EXTRA_DIST =                                                                   \
	org.freedesktop.UDisks2.iscsi.xml                                      \
	udisks2-iscsi.pc.in                                                    \
	$(polkit_in_files)                                                     \
	$(NULL)

clean-local:
	rm -f *~ $(polkit_DATA)
