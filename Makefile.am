## Process this file with automake to produce Makefile.in

NULL =

SUBDIRS = data udisks src tools modules po doc packaging

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

GCC_COLORS ?= 'auto'
export GCC_COLORS

EXTRA_DIST =                                                                   \
	HACKING                                                                \
	README.md                                                              \
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS =                                                    \
	--enable-gtk-doc                                                       \
	--enable-modules                                                       \
	--disable-introspection                                                \
	--with-udevdir=$$dc_install_base/$(udevdir)                            \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)  \
	$(NULL)

sign: dist
	gpg --armor --detach-sign --output storaged-$(VERSION).tar.bz2.sign storaged-$(VERSION).tar.bz2

shortlog:
	git shortlog -r $$(git tag -l | tail -n 1)..HEAD

clean-local:
	rm -f *~

RPMDIR = `readlink -f $(top_builddir)/packaging`
SRCDIR = `readlink -f $(top_srcdir)`
rpm: dist-bzip2
	rpmbuild \
		--define "_sourcedir $(SRCDIR)" \
		--define "_specdir $(RPMDIR)" \
		--define "_builddir $(RPMDIR)" \
		--define "_srcrpmdir $(RPMDIR)" \
		--define "_rpmdir $(RPMDIR)" \
		-ba $(SRCDIR)/packaging/storaged.spec
