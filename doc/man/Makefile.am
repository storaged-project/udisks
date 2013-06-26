
NULL =

man_MANS =

if ENABLE_MAN
man_MANS +=				\
	udisksctl.1			\
	udisksd.8			\
	udisks.8			\
	umount.udisks2.8		\
	$(NULL)
endif

udisksctl.1 : udisksctl.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisksd.8 : udisksd.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisks.8 : udisks.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

umount.udisks2.8 : umount.udisks2.xml
	$(XSLTPROC) -path "$(builddir)/.." -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

EXTRA_DIST= 				\
	udisksctl.xml			\
	udisksd.xml			\
	udisks.xml             		\
	umount.udisks2.xml		\
	$(NULL)

clean-local:
	rm -f *~ *.[18]
