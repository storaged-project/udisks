
NULL =

SUBDIRS = man

# The name of the module.
DOC_MODULE=storaged

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.xml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS=

# Types
STORAGED_TYPES=$(DOC_MODULE).types

# Sections
STORAGED_SECTIONS=$(DOC_MODULE)-sections.txt

# The directory containing the source code. Relative to $(srcdir)
DOC_SOURCE_DIR = $(top_srcdir)

HFILE_GLOB = 				\
	$(top_srcdir)/storaged/*.h 	\
	$(top_srcdir)/src/*.h 		\
	$(top_srcdir)/modules/*.h

CFILE_GLOB = 				\
	$(top_srcdir)/storaged/*.c 	\
	$(top_srcdir)/src/*.c 		\
	$(top_srcdir)/modules/*.c

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

GTKDOC_LIBS = 							\
	$(GLIB_LIBS)						\
	$(GIO_LIBS)						\
	$(top_builddir)/storaged/libstoraged.la			\
	$(top_builddir)/src/libstoraged-daemon.la		\
	$(NULL)

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --output-format=xml --sgml-mode --name-space=storaged

# Images to copy into HTML directory
HTML_IMAGES =			\
	$(NULL)

content_files =			\
	$(NULL)

expand_content_files =		\
	$(NULL)

extra_files =			\
	$(NULL)

# ------------------------------------------------------------------------------

all-local: storaged-docs-generated

storaged-docs-generated: storaged-docs storaged-sections storaged-types

STORAGED_DEPS=storaged-docs-generated

$(DOC_MAIN_SGML_FILE): storaged-docs

storaged-docs: $(DOC_MAIN_SGML_FILE).in $(DOC_MAIN_SGML_FILE).lvm2.sed
if HAVE_LVM2
	$(AM_V_GEN) $(SED) -f $(DOC_MAIN_SGML_FILE).lvm2.sed $< > $(DOC_MAIN_SGML_FILE)
else
	$(AM_V_GEN) cp $< $(DOC_MAIN_SGML_FILE)
endif # HAVE_LVM2

storaged-sections: $(STORAGED_SECTIONS).in $(STORAGED_SECTIONS).lvm2.sed
if HAVE_LVM2
	$(AM_V_GEN) $(SED) -f $(STORAGED_SECTIONS).lvm2.sed $< > $(STORAGED_SECTIONS)
else
	$(AM_V_GEN) $(SED) "/<\!-- LVM2_GENERATED_SECTIONS -->/d" $< > $(STORAGED_SECTIONS)
endif # HAVE_LVM2

storaged-types: $(STORAGED_TYPES).in $(STORAGED_TYPES).lvm2.sed
if HAVE_LVM2
	$(AM_V_GEN) $(SED) -f $(STORAGED_TYPES).lvm2.sed $< > $(STORAGED_TYPES)
else
	$(AM_V_GEN) $(SED) "/\# LVM2_TYPES/d" $< > $(STORAGED_TYPES)
endif # HAVE_LVM2

# GTKDOC for LVM2
if HAVE_LVM2
DOC_SOURCE_DIR += $(top_srcdir)/modules/lvm2
HFILE_GLOB += $(top_srcdir)/modules/lvm2/*.h
CFILE_GLOB += $(top_srcdir)/modules/lvm2/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/lvm2
GTKDOC_LIBS += $(top_builddir)/modules/lvm2/libstoraged_lvm2.la
endif # HAVE_LVM2

# ------------------------------------------------------------------------------

include $(top_srcdir)/gtk-doc.make

EXTRA_DIST +=					\
	$(DOC_MAIN_SGML_FILE).in		\
	$(DOC_MAIN_SGML_FILE).lvm2.dbus		\
	$(DOC_MAIN_SGML_FILE).lvm2.generated	\
	$(DOC_MAIN_SGML_FILE).lvm2.sed		\
	$(STORAGED_TYPES).in			\
	$(STORAGED_TYPES).lvm2			\
	$(STORAGED_TYPES).lvm2.sed		\
	$(STORAGED_SECTIONS).in         	\
	$(STORAGED_SECTIONS).lvm2.sections	\
	$(STORAGED_SECTIONS).lvm2.sed		\
	version.xml.in

CLEANFILES +=   			\
	$(DOC_MAIN_SGML_FILE)		\
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
