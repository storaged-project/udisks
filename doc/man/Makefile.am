
NULL =

man_MANS =

if ENABLE_MAN
man_MANS +=                                                                    \
	udisksctl.1                                                            \
	udisksd.8                                                              \
	udisks.8                                                               \
	umount.udisks2.8                                                       \
	$(NULL)

if HAVE_LVM2
man_MANS += udisks-lvm.8
endif # HAVE_LVM2
endif # ENABLE_MAN

udisksctl.1 : udisksctl.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisks.8 : udisks.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisksd.8 : udisksd.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

umount.udisks2.8 : umount.udisks2.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

if HAVE_LVM2
udisks-lvm.8 : udisks-lvm.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<
endif # HAVE_LVM2

if HAVE_LSM
notrans_dist_man5_MANS=udisks2_lsm.conf.5
endif

EXTRA_DIST =                                                                   \
	udisksctl.xml                                                          \
	udisksd.xml                                                            \
	udisks-lvm.xml                                                         \
	udisks.xml                                                             \
	umount.udisks2.xml                                                     \
	$(NULL)

clean-local:
	rm -f *~ *.[18]
