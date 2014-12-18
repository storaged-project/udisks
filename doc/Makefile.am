
NULL =

SUBDIRS = man

# The name of the module.
DOC_MODULE=udisks2

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.xml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS=

# The directory containing the source code. Relative to $(srcdir)
DOC_SOURCE_DIR=$(top_srcdir)

HFILE_GLOB=$(top_srcdir)/udisks/*.h $(top_srcdir)/src/*.h
CFILE_GLOB=$(top_srcdir)/udisks/*.c $(top_srcdir)/src/*.c

# Headers to ignore
IGNORE_HFILES=				\
	config.h			\
	$(NULL)

# CFLAGS and LDFLAGS for compiling scan program. Only needed
# if $(DOC_MODULE).types is non-empty.
AM_CPPFLAGS = \
	-I$(srcdir)			\
	-I$(top_srcdir)			\
	-I$(top_builddir)		\
	$(GLIB_CFLAGS)			\
	$(GIO_CFLAGS)			\
	$(NULL)

GTKDOC_LIBS = 					\
	$(GLIB_LIBS)				\
	$(GIO_LIBS)				\
	$(top_builddir)/udisks/libudisks2.la	\
	$(top_builddir)/src/libudisks-daemon.la	\
	$(NULL)

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --output-format=xml --sgml-mode --name-space=udisks

# Images to copy into HTML directory
HTML_IMAGES =			\
	$(NULL)

content_files =			\
	$(NULL)

expand_content_files =		\
	$(NULL)

extra_files =			\
	$(NULL)

include $(top_srcdir)/gtk-doc.make


EXTRA_DIST +=			\
	version.xml.in

CLEANFILES +=   		\
	$(DOC_MODULE)-decl-list.txt	\
	$(DOC_MODULE)-decl.txt		\
	$(DOC_MODULE)-overrides.txt	\
	$(DOC_MODULE)-undeclared.txt	\
	$(DOC_MODULE)-undocumented.txt	\
	$(DOC_MODULE)-overrides.txt	\
	$(DOC_MODULE)-unused.txt	\
	$(DOC_MODULE).args		\
	$(DOC_MODULE).hierarchy		\
	$(DOC_MODULE).interfaces	\
	$(DOC_MODULE).prerequisites	\
	$(DOC_MODULE).signals		\
	*.stamp				\
	-rf xml html tmpl		\
	$(NULL)
