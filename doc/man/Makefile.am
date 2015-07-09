
NULL =

man_MANS =

if ENABLE_MAN
man_MANS +=                                                                    \
	storagedctl.1                                                          \
	storaged.8                                                             \
	storaged-project.8                                                     \
	umount.storaged.8                                                      \
	$(NULL)

if HAVE_LVM2
man_MANS += storaged-lvm.8
endif # HAVE_LVM2
endif # ENABLE_MAN

storagedctl.1 : storagedctl.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

storaged.8 : storaged.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

storaged-project.8 : storaged-project.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

umount.storaged.8 : umount.storaged.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

if HAVE_LVM2
storaged-lvm.8 : storaged-lvm.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<
endif # HAVE_LVM2

if HAVE_LSM
notrans_dist_man5_MANS=storaged_lsm.conf.5
endif

EXTRA_DIST =                                                                   \
	storagedctl.xml                                                        \
	storaged.xml                                                           \
	storaged-lvm.xml                                                       \
	storaged-project.xml                                                   \
	umount.storaged.xml                                                    \
	$(NULL)

clean-local:
	rm -f *~ *.[18]
