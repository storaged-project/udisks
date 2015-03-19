
NULL =

man_MANS =

if ENABLE_MAN
man_MANS +=				\
	storagedctl.1			\
	storaged.8			\
	storaged-project.8		\
	umount.storaged.8		\
	$(NULL)
endif

storagedctl.1 : storagedctl.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

storaged.8 : storaged.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

storaged-project.8 : storaged-project.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

umount.storaged.8 : umount.storaged.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

EXTRA_DIST= 				\
	storagedctl.xml			\
	storaged.xml			\
	storaged-project.xml		\
	umount.storaged.xml		\
	$(NULL)

clean-local:
	rm -f *~ *.[18]
