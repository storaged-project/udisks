## Process this file with automake to produce Makefile.in

NULL =

SUBDIRS =

LVM2_MODULE_DIR = $(top_srcdir)/modules/lvm2

$(dbus_built_sources) : Makefile.am $(top_srcdir)/data/org.freedesktop.UDisks2.xml
	gdbus-codegen                                                   \
	    --interface-prefix org.freedesktop.UDisks2.                 \
	    --c-namespace UDisks                                        \
	    --c-generate-object-manager                                 \
	    --output-directory $(top_srcdir)/udisks                     \
	    --generate-c-code udisks-generated                          \
	    --generate-docbook udisks-generated-doc                     \
	    $(top_srcdir)/data/org.freedesktop.UDisks2.xml              \
	    $(NULL)

dbus_built_sources =                                                \
    $(top_srcdir)/udisks/udisks-generated.h                         \
    $(top_srcdir)/udisks/udisks-generated.c                         \
    $(NULL)


$(dbus_lvm2_built_sources): Makefile.am $(LVM2_MODULE_DIR)/data/org.freedesktop.UDisks2.lvm2.xml
	gdbus-codegen                                                   \
	    --interface-prefix org.freedesktop.UDisks2.                 \
	    --c-namespace UDisks                                        \
	    --output-directory $(LVM2_MODULE_DIR)                       \
	    --generate-c-code udisks-lvm2-generated                     \
	    --generate-docbook udisks-generated-doc                     \
	    $(LVM2_MODULE_DIR)/data/org.freedesktop.UDisks2.lvm2.xml    \
	    $(NULL)

dbus_lvm2_built_sources =                                           \
	$(LVM2_MODULE_DIR)/udisks-lvm2-generated.h                      \
	$(LVM2_MODULE_DIR)/udisks-lvm2-generated.c                      \
	$(NULL)

BUILT_SOURCES =                                                     \
	$(dbus_built_sources)                                           \
	$(dbus_lvm2_built_sources)                                      \
	$(NULL)

CLEANFILES =                                                        \
	$(NULL)

EXTRA_DIST =                                                        \
	$(NULL)

