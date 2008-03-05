
NULL = 

if MAN_PAGES_ENABLED

man_MANS = 				\
	   DeviceKit-disks.8		\
	   $(NULL)

%.8 : %.xml
	$(XSLTPROC) -nonet http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

endif # MAN_PAGES_ENABLED

EXTRA_DIST= 				    \
	    DeviceKit-disks.xml             \
	    $(NULL)

clean-local:
	rm -f *~ *.8
