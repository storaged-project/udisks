
NULL =

man_MANS =

if ENABLE_MAN
man_MANS +=                                                                    \
	udisksctl.1                                                            \
	udisksd.8                                                              \
	udisks.8                                                               \
	umount.udisks2.8                                                       \
	udisks2.conf.5                                                         \
	$(NULL)

if HAVE_LSM
man_MANS += udisks2_lsm.conf.5
endif # HAVE_LSM
endif # ENABLE_MAN

udisksctl.1 : udisksctl.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisks.8 : udisks.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisksd.8 : udisksd.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

umount.udisks2.8 : umount.udisks2.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

# Hack: Autotools expand @sysconfdir@ as %{prefix}/etc; that's why we sed this
# to real prefix, so it doesn't appear like this in the man page.
udisks2.conf.5.xml: udisks2.conf.5.xml.in
	$(SED) "s|\$${prefix}|${prefix}|" $< > $@

udisks2.conf.5 : udisks2.conf.5.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

if HAVE_LSM
# Hack: Autotools expand @sysconfdir@ as %{prefix}/etc; that's why we sed this
# to real prefix, so it doesn't appear like this in the man page.
udisks2_lsm.conf.5.xml: udisks2_lsm.conf.5.xml.in
	$(SED) "s|\$${prefix}|${prefix}|" $< > $@

udisks2_lsm.conf.5 : udisks2_lsm.conf.5.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<
endif # HAVE_LSM

EXTRA_DIST =                                                                   \
	udisksctl.xml                                                          \
	udisksd.xml                                                            \
	udisks2.conf.5.xml                                                     \
	udisks.xml                                                             \
	umount.udisks2.xml                                                     \
	$(NULL)

if HAVE_LSM
EXTRA_DIST += udisks2_lsm.conf.5.xml
endif # HAVE_LSM

clean-local:
	rm -f *~ *.[18]                                                        \
	udisks2.conf.5                                                        \
	udisks2.conf.5.xml                                                    \
	udisks2_lsm.conf.5                                                    \
	udisks2_lsm.conf.5.xml
