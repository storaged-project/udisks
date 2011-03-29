## Process this file with automake to produce Makefile.in

NULL =

# NOTE: 'tests' subdir is disabled for now
SUBDIRS = data udisks src tools policy po doc

EXTRA_DIST = 			\
	HACKING 		\
	$(NULL)

# xsltproc barfs on 'make distcheck'; disable for now
DISTCHECK_CONFIGURE_FLAGS=--disable-man-pages --disable-gtk-doc

clean-local :
	rm -f *~
