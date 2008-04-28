
NULL =

if MAN_PAGES_ENABLED

man_MANS = 				\
	devkit-disks.1			\
	devkit-disks-daemon.8		\
	DeviceKit-disks.7		\
	$(NULL)

%.1 %.7 %.8 : %.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

endif # MAN_PAGES_ENABLED

EXTRA_DIST= 				\
	devkit-disks.xml		\
	devkit-disks-daemon.xml		\
	DeviceKit-disks.xml             \
	$(NULL)

clean-local:
	rm -f *~ *.[178]
