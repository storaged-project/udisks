## Process this file with automake to produce Makefile.in

NULL =

SUBDIRS = data udisks src tools po doc

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

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
	scp udisks-$(VERSION).tar.bz2 udisks-$(VERSION).tar.bz2.sign "people.freedesktop.org:/srv/udisks.freedesktop.org/www/releases/"

publish-docs :
	gtkdoc-rebase --html-dir doc/html --online
	ssh "people.freedesktop.org" "mkdir -p /srv/udisks.freedesktop.org/www/docs/$(VERSION)"
	scp doc/html/* "people.freedesktop.org:/srv/udisks.freedesktop.org/www/docs/$(VERSION)"
	ssh "people.freedesktop.org" "rm -f /srv/udisks.freedesktop.org/www/docs/latest; ln -s $(VERSION) /srv/udisks.freedesktop.org/www/docs/latest"

# publish docs readable from http://udisks.freedesktop.org/docs/master/ (instead of latest)
#
# This is useful for work-in-progress when hacking on master and you want
# to easily share API ideas etc. with others without requiring them to
# build master themselves
publish-docs-master :
	gtkdoc-rebase --html-dir doc/html --online
	ssh "people.freedesktop.org" "rm -rf /srv/udisks.freedesktop.org/www/docs/master; mkdir -p /srv/udisks.freedesktop.org/www/docs/master"
	scp doc/html/* "people.freedesktop.org:/srv/udisks.freedesktop.org/www/docs/master"

clean-local :
	rm -f *~
