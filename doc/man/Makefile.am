
NULL =

if MAN_PAGES_ENABLED

man_MANS = 				\
	udisks.1			\
	udisks-tcp-bridge.1		\
	udisks-daemon.8			\
	udisks.7			\
	$(NULL)

udisks.1 : udisks.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisks-tcp-bridge.1 : udisks-tcp-bridge.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisks-daemon.8 : udisks-daemon.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

udisks.7 : udisks7.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

endif # MAN_PAGES_ENABLED

EXTRA_DIST= 				\
	udisks.xml			\
	udisks-daemon.xml		\
	udisks7.xml             	\
	$(NULL)

clean-local:
	rm -f *~ *.[178]
