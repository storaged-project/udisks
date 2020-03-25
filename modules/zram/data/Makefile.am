
NULL = 

polkitdir       = $(datadir)/polkit-1/actions
polkit_in_files = org.freedesktop.UDisks2.zram.policy.in
polkit_DATA     = $(polkit_in_files:.policy.in=.policy)

$(polkit_DATA): $(polkit_in_files)
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

systemdsystemunit_DATA = zram-setup@.service

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = udisks2-zram.pc

EXTRA_DIST =                                                                   \
	org.freedesktop.UDisks2.zram.xml                                       \
	zram-setup@.service                                                    \
	udisks2-zram.pc.in                                                     \
	$(polkit_in_files)                                                     \
	$(NULL)

clean-local:
	rm -f *~ $(polkit_DATA)

