
NULL =

CONF_FILE = lvm.conf
CONF_FILEPATH=$(libdir)/storaged/lvm-nolocking/$(CONF_FILE)

polkitdir        = $(datadir)/polkit-1/actions
polkit_in_files  = org.storaged.Storaged.lvm2.policy.in
polkit_DATA      = $(polkit_in_files:.policy.in=.policy)

@INTLTOOL_POLICY_RULE@

install-data-local:
	$(MKDIR_P) $(DESTDIR)$(libdir)/storaged/lvm-nolocking
	$(INSTALL_DATA) $(srcdir)/lvm-nolocking.conf                           \
	                $(DESTDIR)/$(CONF_FILEPATH)

EXTRA_DIST =                                                                   \
	org.storaged.Storaged.lvm2.xml                                         \
	lvm-nolocking.conf                                                     \
	$(polkit_in_files)                                                     \
	$(NULL)

clean-local:
	rm -f *~ $(polkit_DATA)

uninstall-local:
	rm $(DESTDIR)/$(CONF_FILEPATH)
