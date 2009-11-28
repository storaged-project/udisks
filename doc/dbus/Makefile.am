
all : org.freedesktop.DeviceKit.Disks.ref.xml org.freedesktop.DeviceKit.Disks.Device.ref.xml org.freedesktop.DeviceKit.Disks.Adapter.ref.xml org.freedesktop.DeviceKit.Disks.Port.ref.xml

org.freedesktop.DeviceKit.Disks.ref.xml : $(top_srcdir)/data/org.freedesktop.DeviceKit.Disks.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

org.freedesktop.DeviceKit.Disks.Device.ref.xml : $(top_srcdir)/data/org.freedesktop.DeviceKit.Disks.Device.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

org.freedesktop.DeviceKit.Disks.Adapter.ref.xml : $(top_srcdir)/data/org.freedesktop.DeviceKit.Disks.Adapter.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

org.freedesktop.DeviceKit.Disks.Port.ref.xml : $(top_srcdir)/data/org.freedesktop.DeviceKit.Disks.Port.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

EXTRA_DIST = spec-to-docbook.xsl dbus-introspect-docs.dtd

clean-local :
	rm -f *~ *.ref.xml
