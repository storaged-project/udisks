## Process this file with automake to produce Makefile.in

NULL =

SUBDIRS = data udisks src tools po doc

EXTRA_DIST = 			\
	HACKING 		\
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS=							\
	--enable-gtk-doc 							\
	--disable-introspection							\
	--with-udevdir=$$dc_install_base/$(udevdir)				\
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)	\
	$(NULL)

sign : dist
	gpg --armor --detach-sign --output udisks-$(VERSION).tar.bz2.sign udisks-$(VERSION).tar.bz2

publish : sign
	scp udisks-$(VERSION).tar.bz2 udisks-$(VERSION).tar.bz2.sign "david@people.freedesktop.org:/srv/udisks.freedesktop.org/www/releases/"

publish-docs :
	gtkdoc-rebase --html-dir doc/html --online
	ssh "david@people.freedesktop.org" "mkdir -p /srv/udisks.freedesktop.org/www/docs/$(VERSION)"
	scp doc/html/* "david@people.freedesktop.org:/srv/udisks.freedesktop.org/www/docs/$(VERSION)"
	ssh "david@people.freedesktop.org" "rm -f /srv/udisks.freedesktop.org/www/docs/latest; ln -s $(VERSION) /srv/udisks.freedesktop.org/www/docs/latest"

clean-local :
	rm -f *~
