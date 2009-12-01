
all : org.freedesktop.UDisks.ref.xml org.freedesktop.UDisks.Device.ref.xml org.freedesktop.UDisks.Adapter.ref.xml org.freedesktop.UDisks.Port.ref.xml org.freedesktop.UDisks.Expander.ref.xml

org.freedesktop.UDisks.ref.xml : $(top_srcdir)/data/org.freedesktop.UDisks.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

org.freedesktop.UDisks.Device.ref.xml : $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

org.freedesktop.UDisks.Adapter.ref.xml : $(top_srcdir)/data/org.freedesktop.UDisks.Adapter.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

org.freedesktop.UDisks.Port.ref.xml : $(top_srcdir)/data/org.freedesktop.UDisks.Port.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

org.freedesktop.UDisks.Expander.ref.xml : $(top_srcdir)/data/org.freedesktop.UDisks.Expander.xml $(top_srcdir)/doc/dbus/spec-to-docbook.xsl
	echo "<?xml version=\"1.0\"?>""<!DOCTYPE refentry PUBLIC \"-//OASIS//DTD DocBook XML V4.1.2//EN\" \"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd\">" > $@
	$(XSLTPROC) -nonet $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 >> $@

EXTRA_DIST = spec-to-docbook.xsl dbus-introspect-docs.dtd

clean-local :
	rm -f *~ *.ref.xml
