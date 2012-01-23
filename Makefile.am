## Process this file with automake to produce Makefile.in

NULL =

SUBDIRS = data udisks src tools po doc

EXTRA_DIST = 			\
	HACKING 		\
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)

clean-local :
	rm -f *~
