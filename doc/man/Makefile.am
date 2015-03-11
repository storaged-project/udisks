
NULL =

man_MANS =

if ENABLE_MAN
man_MANS +=				\
	storagedctl.1			\
	storaged-daemon.8		\
	storaged.8			\
	umount.storaged.8		\
	$(NULL)
endif

storagedctl.1 : storagedctl.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

storaged-daemon.8 : storaged-daemon.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

storaged.8 : storaged.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

umount.storaged.8 : umount.storaged.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

EXTRA_DIST= 				\
	storagedctl.xml			\
	storaged-daemon.xml		\
	storaged.xml          		\
	umount.storaged.xml		\
	$(NULL)

clean-local:
	rm -f *~ *.[18]
