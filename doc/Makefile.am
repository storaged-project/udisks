
NULL =

SUBDIRS = man

DOC_DIR = $(top_srcdir)/doc

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

HFILE_GLOB =                                                                   \
	$(top_srcdir)/storaged/*.h                                             \
	$(top_srcdir)/src/*.h                                                  \
	$(top_srcdir)/modules/*.h

CFILE_GLOB =                                                                   \
	$(top_srcdir)/storaged/*.c                                             \
	$(top_srcdir)/src/*.c                                                  \
	$(top_srcdir)/modules/*.c

# Headers to ignore
IGNORE_HFILES=                                                                 \
	config.h                                                               \
	$(NULL)

# CFLAGS and LDFLAGS for compiling scan program. Only needed
# if $(DOC_MODULE).types is non-empty.
AM_CPPFLAGS =                                                                  \
	-I$(srcdir)                                                            \
	-I$(top_srcdir)                                                        \
	-I$(top_builddir)                                                      \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(NULL)

GTKDOC_LIBS =                                                                  \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(top_builddir)/storaged/libstoraged.la                                \
	$(top_builddir)/src/libstoraged-daemon.la                              \
	$(NULL)

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --output-format=xml --sgml-mode --name-space=storaged

# Images to copy into HTML directory
HTML_IMAGES =                                                                  \
	$(NULL)

content_files =                                                                \
	$(NULL)

expand_content_files =                                                         \
	$(NULL)

extra_files =                                                                  \
	$(NULL)

# ------------------------------------------------------------------------------

all-local: $(DOC_MAIN_SGML_FILE)

# storaged-docs.xml, storaged-sections.txt storaged.types
$(DOC_MAIN_SGML_FILE): $(DOC_MAIN_SGML_FILE).in $(STORAGED_SECTIONS).in $(STORAGED_TYPES).in
	$(AM_V_at) rm -f $(DOC_MAIN_SGML_FILE) $(STORAGED_SECTIONS) $(STORAGED_TYPES) && \
	cp $(DOC_MAIN_SGML_FILE).in $(builddir)/$(DOC_MAIN_SGML_FILE) && \
	cp $(STORAGED_SECTIONS).in $(builddir)/$(STORAGED_SECTIONS) && \
	cp $(STORAGED_TYPES).in $(builddir)/$(STORAGED_TYPES)
if HAVE_LVM2
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).lvm2.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(STORAGED_SECTIONS).lvm2.sed $(builddir)/$(STORAGED_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(STORAGED_TYPES).lvm2.sed $(builddir)/$(STORAGED_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- LVM2_GENERATED_SECTIONS -->/d" $(builddir)/$(STORAGED_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# LVM2_TYPES/d" $(builddir)/$(STORAGED_TYPES)
endif # HAVE_LVM2
if HAVE_ISCSI
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).iscsi.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(STORAGED_SECTIONS).iscsi.sed $(builddir)/$(STORAGED_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(STORAGED_TYPES).iscsi.sed $(builddir)/$(STORAGED_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- ISCSI_GENERATED_SECTIONS -->/d" $(builddir)/$(STORAGED_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# ISCSI_TYPES/d" $(builddir)/$(STORAGED_TYPES)
endif # HAVE_ISCSI

# GTKDOC for LVM2
if HAVE_LVM2
DOC_SOURCE_DIR += $(top_srcdir)/modules/lvm2
HFILE_GLOB += $(top_srcdir)/modules/lvm2/*.h
CFILE_GLOB += $(top_srcdir)/modules/lvm2/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/lvm2
GTKDOC_LIBS += $(top_builddir)/modules/lvm2/libstoraged_lvm2.la
endif # HAVE_LVM2

# GTKDOC for ISCSI
if HAVE_ISCSI
DOC_SOURCE_DIR += $(top_srcdir)/modules/iscsi
HFILE_GLOB += $(top_srcdir)/modules/iscsi/*.h
CFILE_GLOB += $(top_srcdir)/modules/iscsi/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/iscsi
GTKDOC_LIBS += $(top_builddir)/modules/iscsi/libstoraged_iscsi.la
endif # HAVE_ISCSI

# ------------------------------------------------------------------------------

include $(top_srcdir)/gtk-doc.make

EXTRA_DIST +=                                                                  \
	$(DOC_MAIN_SGML_FILE).in.in                                            \
	$(DOC_MAIN_SGML_FILE).in                                               \
	$(DOC_MAIN_SGML_FILE).iscsi.dbus.in                                    \
	$(DOC_MAIN_SGML_FILE).iscsi.generated.in                               \
	$(DOC_MAIN_SGML_FILE).iscsi.sed                                        \
	$(DOC_MAIN_SGML_FILE).lvm2.dbus.in                                     \
	$(DOC_MAIN_SGML_FILE).lvm2.generated.in                                \
	$(DOC_MAIN_SGML_FILE).lvm2.sed                                         \
	$(STORAGED_TYPES).in.in                                                \
	$(STORAGED_TYPES).in                                                   \
	$(STORAGED_TYPES).iscsi                                                \
	$(STORAGED_TYPES).iscsi.sed                                            \
	$(STORAGED_TYPES).lvm2                                                 \
	$(STORAGED_TYPES).lvm2.sed                                             \
	$(STORAGED_SECTIONS).in.in                                             \
	$(STORAGED_SECTIONS).in                                                \
	$(STORAGED_SECTIONS).iscsi.sections.in                                 \
	$(STORAGED_SECTIONS).iscsi.sed                                         \
	$(STORAGED_SECTIONS).lvm2.sections.in                                  \
	$(STORAGED_SECTIONS).lvm2.sed                                          \
	version.xml.in                                                         \
	TODO-ISCSI

CLEANFILES +=                                                                  \
	$(DOC_MAIN_SGML_FILE)                                                  \
	$(STORAGED_SECTIONS)                                                   \
	$(STORAGED_TYPES)                                                      \
	$(DOC_MODULE)-decl-list.txt                                            \
	$(DOC_MODULE)-decl.txt                                                 \
	$(DOC_MODULE)-overrides.txt                                            \
	$(DOC_MODULE)-undeclared.txt                                           \
	$(DOC_MODULE)-undocumented.txt                                         \
	$(DOC_MODULE)-overrides.txt                                            \
	$(DOC_MODULE)-unused.txt                                               \
	$(DOC_MODULE).args                                                     \
	$(DOC_MODULE).hierarchy                                                \
	$(DOC_MODULE).interfaces                                               \
	$(DOC_MODULE).prerequisites                                            \
	$(DOC_MODULE).signals                                                  \
	*.stamp                                                                \
	-rf xml html tmpl                                                      \
	$(NULL)
