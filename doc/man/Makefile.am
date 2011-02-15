
NULL =

if MAN_PAGES_ENABLED

man_MANS = 				\
	udisksctl.1			\
	udisksd.8			\
	udisks.7			\
	$(NULL)

udisksctl.1 : udisksctl.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisksd.8 : udisksd.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisks.7 : udisks.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

endif # MAN_PAGES_ENABLED

EXTRA_DIST= 				\
	udisksctl.xml			\
	udisksd.xml			\
	udisks.xml             		\
	$(NULL)

clean-local:
	rm -f *~ *.[178]
