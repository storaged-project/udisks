
NULL =

SUBDIRS = man

DOC_DIR = $(top_srcdir)/doc

# The name of the module.
DOC_MODULE=udisks2

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.xml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS=

# Types
UDISKS_TYPES=$(DOC_MODULE).types

# Sections
UDISKS_SECTIONS=$(DOC_MODULE)-sections.txt

# The directory containing the source code. Relative to $(srcdir)
DOC_SOURCE_DIR = $(top_srcdir)

HFILE_GLOB =                                                                   \
	$(top_srcdir)/udisks/*.h                                               \
	$(top_srcdir)/src/*.h                                                  \
	$(top_srcdir)/modules/*.h

CFILE_GLOB =                                                                   \
	$(top_srcdir)/udisks/*.c                                               \
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
	$(top_builddir)/udisks/libudisks2.la                                   \
	$(top_builddir)/src/libudisks-daemon.la                                \
	$(NULL)

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --output-format=xml --sgml-mode --name-space=udisks

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

all-local: generate-main-sgml-file

# New gtk-doc 2.16 for some reason automatically creates a no-op
# "$(DOC_MAIN_SGML_FILE)" make target that overrides our target
$(DOC_MAIN_SGML_FILE): generate-main-sgml-file

# udisks2-docs.xml, udisks2-sections.txt udisks2.types
generate-main-sgml-file: $(DOC_MAIN_SGML_FILE).in $(UDISKS_SECTIONS).in $(UDISKS_TYPES).in
	$(AM_V_at) rm -f $(DOC_MAIN_SGML_FILE) $(UDISKS_SECTIONS) $(UDISKS_TYPES) && \
	cp $(DOC_MAIN_SGML_FILE).in $(builddir)/$(DOC_MAIN_SGML_FILE) && \
	cp $(UDISKS_SECTIONS).in $(builddir)/$(UDISKS_SECTIONS) && \
	cp $(UDISKS_TYPES).in $(builddir)/$(UDISKS_TYPES)
if HAVE_LVM2
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).lvm2.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_SECTIONS).lvm2.sed $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_TYPES).lvm2.sed $(builddir)/$(UDISKS_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- LVM2_GENERATED_SECTIONS -->/d" $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# LVM2_TYPES/d" $(builddir)/$(UDISKS_TYPES)
endif # HAVE_LVM2
if HAVE_LSM
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).lsm.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_SECTIONS).lsm.sed $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_TYPES).lsm.sed $(builddir)/$(UDISKS_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- LSM_GENERATED_SECTIONS -->/d" $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# LSM_TYPES/d" $(builddir)/$(UDISKS_TYPES)
endif # HAVE_LSM
if HAVE_ISCSI
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).iscsi.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_SECTIONS).iscsi.sed $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_TYPES).iscsi.sed $(builddir)/$(UDISKS_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- ISCSI_GENERATED_SECTIONS -->/d" $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# ISCSI_TYPES/d" $(builddir)/$(UDISKS_TYPES)
endif # HAVE_ISCSI
if HAVE_BTRFS
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).btrfs.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_SECTIONS).btrfs.sed $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_TYPES).btrfs.sed $(builddir)/$(UDISKS_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- BTRFS_GENERATED_SECTIONS -->/d" $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# BTRFS_TYPES/d" $(builddir)/$(UDISKS_TYPES)
endif # HAVE_BTRFS
if HAVE_ZRAM
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).zram.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_SECTIONS).zram.sed $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_TYPES).zram.sed $(builddir)/$(UDISKS_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- ZRAM_GENERATED_SECTIONS -->/d" $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# ZRAM_TYPES/d" $(builddir)/$(UDISKS_TYPES)
endif # HAVE_ZRAM
if HAVE_BCACHE
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).bcache.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_SECTIONS).bcache.sed $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_TYPES).bcache.sed $(builddir)/$(UDISKS_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- BCACHE_GENERATED_SECTIONS -->/d" $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# BCACHE_TYPES/d" $(builddir)/$(UDISKS_TYPES)
endif # HAVE_BCACHE
if HAVE_VDO
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(DOC_MAIN_SGML_FILE).vdo.sed $(builddir)/$(DOC_MAIN_SGML_FILE)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_SECTIONS).vdo.sed $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i -f $(DOC_DIR)/$(UDISKS_TYPES).vdo.sed $(builddir)/$(UDISKS_TYPES)
else
	$(AM_V_at) $(SED) -i "/<\!-- VDO_GENERATED_SECTIONS -->/d" $(builddir)/$(UDISKS_SECTIONS)
	$(AM_V_at) $(SED) -i "/\# VDO_TYPES/d" $(builddir)/$(UDISKS_TYPES)
endif # HAVE_VDO

# GTKDOC for LVM2
if HAVE_LVM2
DOC_SOURCE_DIR += $(top_srcdir)/modules/lvm2
HFILE_GLOB += $(top_srcdir)/modules/lvm2/*.h
CFILE_GLOB += $(top_srcdir)/modules/lvm2/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/lvm2
GTKDOC_LIBS += $(top_builddir)/modules/lvm2/libudisks2_lvm2.la
endif # HAVE_LVM2

# GTKDOC for ISCSI
if HAVE_ISCSI
DOC_SOURCE_DIR += $(top_srcdir)/modules/iscsi
HFILE_GLOB += $(top_srcdir)/modules/iscsi/*.h
CFILE_GLOB += $(top_srcdir)/modules/iscsi/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/iscsi
GTKDOC_LIBS += $(top_builddir)/modules/iscsi/libudisks2_iscsi.la
endif # HAVE_ISCSI

# GTKDOC for BTRFS
if HAVE_BTRFS
DOC_SOURCE_DIR += $(top_srcdir)/modules/btrfs
HFILE_GLOB += $(top_srcdir)/modules/btrfs/*.h
CFILE_GLOB += $(top_srcdir)/modules/btrfs/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/btrfs
GTKDOC_LIBS += $(top_builddir)/modules/btrfs/libudisks2_btrfs.la
endif # HAVE_BTRFS

# GTKDOC for ZRAM
if HAVE_ZRAM
DOC_SOURCE_DIR += $(top_srcdir)/modules/zram
HFILE_GLOB += $(top_srcdir)/modules/zram/*.h
CFILE_GLOB += $(top_srcdir)/modules/zram/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/zram
GTKDOC_LIBS += $(top_builddir)/modules/zram/libudisks2_zram.la
endif # HAVE_ZRAM

# GTKDOC for LSM
if HAVE_LSM
DOC_SOURCE_DIR += $(top_srcdir)/modules/lsm
HFILE_GLOB += $(top_srcdir)/modules/lsm/*.h
CFILE_GLOB += $(top_srcdir)/modules/lsm/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/lsm
GTKDOC_LIBS += $(top_builddir)/modules/lsm/libudisks2_lsm.la
endif # HAVE_LSM

# GTKDOC for BCACHE
if HAVE_BCACHE
DOC_SOURCE_DIR += $(top_srcdir)/modules/bcache
HFILE_GLOB += $(top_srcdir)/modules/bcache/*.h
CFILE_GLOB += $(top_srcdir)/modules/bcache/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/bcache
GTKDOC_LIBS += $(top_builddir)/modules/bcache/libudisks2_bcache.la
endif # HAVE_BCACHE

# GTKDOC for VDO
if HAVE_VDO
DOC_SOURCE_DIR += $(top_srcdir)/modules/vdo
HFILE_GLOB += $(top_srcdir)/modules/vdo/*.h
CFILE_GLOB += $(top_srcdir)/modules/vdo/*.c
AM_CPPFLAGS += -I$(top_srcdir)/modules/vdo
GTKDOC_LIBS += $(top_builddir)/modules/vdo/libudisks2_vdo.la
endif # HAVE_VDO

# ------------------------------------------------------------------------------

include $(top_srcdir)/gtk-doc.make

EXTRA_DIST +=                                                                  \
	$(DOC_MAIN_SGML_FILE).in.in                                            \
	$(DOC_MAIN_SGML_FILE).in                                               \
        $(DOC_MAIN_SGML_FILE).bcache.dbus.in                                   \
        $(DOC_MAIN_SGML_FILE).bcache.generated.in                              \
        $(DOC_MAIN_SGML_FILE).bcache.sed                                       \
	$(DOC_MAIN_SGML_FILE).btrfs.dbus.in                                    \
	$(DOC_MAIN_SGML_FILE).btrfs.generated.in                               \
	$(DOC_MAIN_SGML_FILE).btrfs.sed                                        \
	$(DOC_MAIN_SGML_FILE).iscsi.dbus.in                                    \
	$(DOC_MAIN_SGML_FILE).iscsi.generated.in                               \
	$(DOC_MAIN_SGML_FILE).iscsi.sed                                        \
	$(DOC_MAIN_SGML_FILE).lsm.dbus.in                                      \
	$(DOC_MAIN_SGML_FILE).lsm.generated.in                                 \
	$(DOC_MAIN_SGML_FILE).lsm.sed                                          \
	$(DOC_MAIN_SGML_FILE).lvm2.dbus.in                                     \
	$(DOC_MAIN_SGML_FILE).lvm2.generated.in                                \
	$(DOC_MAIN_SGML_FILE).lvm2.sed                                         \
	$(DOC_MAIN_SGML_FILE).vdo.dbus.in                                      \
	$(DOC_MAIN_SGML_FILE).vdo.generated.in                                 \
	$(DOC_MAIN_SGML_FILE).vdo.sed                                          \
	$(DOC_MAIN_SGML_FILE).zram.dbus.in                                     \
	$(DOC_MAIN_SGML_FILE).zram.generated.in                                \
	$(DOC_MAIN_SGML_FILE).zram.block_devices.in                            \
	$(DOC_MAIN_SGML_FILE).zram.sed                                         \
	$(UDISKS_TYPES).in.in                                                  \
	$(UDISKS_TYPES).in                                                     \
        $(UDISKS_TYPES).bcache.in                                              \
        $(UDISKS_TYPES).bcache.sed                                             \
	$(UDISKS_TYPES).btrfs.in                                               \
	$(UDISKS_TYPES).btrfs.sed                                              \
	$(UDISKS_TYPES).iscsi.in                                               \
	$(UDISKS_TYPES).iscsi.sed                                              \
	$(UDISKS_TYPES).lsm.in                                                 \
	$(UDISKS_TYPES).lsm.sed                                                \
	$(UDISKS_TYPES).lvm2.in                                                \
	$(UDISKS_TYPES).lvm2.sed                                               \
	$(UDISKS_TYPES).vdo.in                                                 \
	$(UDISKS_TYPES).vdo.sed                                                \
	$(UDISKS_TYPES).zram.in                                                \
	$(UDISKS_TYPES).zram.sed                                               \
	$(UDISKS_SECTIONS).in.in                                               \
	$(UDISKS_SECTIONS).in                                                  \
        $(UDISKS_SECTIONS).bcache.sections.in                                  \
        $(UDISKS_SECTIONS).bcache.sed                                          \
	$(UDISKS_SECTIONS).btrfs.sections.in                                   \
	$(UDISKS_SECTIONS).btrfs.sed                                           \
	$(UDISKS_SECTIONS).iscsi.sections.in                                   \
	$(UDISKS_SECTIONS).iscsi.sed                                           \
	$(UDISKS_SECTIONS).lsm.sections.in                                     \
	$(UDISKS_SECTIONS).lsm.sed                                             \
	$(UDISKS_SECTIONS).lvm2.sections.in                                    \
	$(UDISKS_SECTIONS).lvm2.sed                                            \
	$(UDISKS_SECTIONS).vdo.sections.in                                     \
	$(UDISKS_SECTIONS).vdo.sed                                             \
	$(UDISKS_SECTIONS).zram.sections.in                                    \
	$(UDISKS_SECTIONS).zram.sed                                            \
	$(UDISKS_SECTIONS).lsm.sections.in                                     \
	version.xml.in                                                         \
	TODO-ISCSI

CLEANFILES +=                                                                  \
	$(DOC_MAIN_SGML_FILE)                                                  \
	$(UDISKS_SECTIONS)                                                     \
	$(UDISKS_TYPES)                                                        \
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

.PHONY: generate-main-sgml-file
