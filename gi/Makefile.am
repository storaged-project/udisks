## Process this file with automake to produce Makefile.in

SUBDIRS =

NULL =

MODULES_DIR = ../modules

LVM2_MODULE_DIR = $(MODULES_DIR)/lvm2
UDISKS_DIR = ../udisks

# LVM2 module ------------------------------------------------------------

lvm2_SOURCES =                                            \
	$(LVM2_MODULE_DIR)/udisks-lvm2-generated.h            \
	$(LVM2_MODULE_DIR)/udisks-lvm2-generated.c            \
	$(MODULES_DIR)/udisksmoduleiface.h                    \
	$(MODULES_DIR)/udisksmoduleifacetypes.h               \
	$(MODULES_DIR)/udisksmoduleobject.h                   \
	$(MODULES_DIR)/udisksmoduleobject.c                   \
	$(LVM2_MODULE_DIR)/udiskslvm2types.h                  \
	$(LVM2_MODULE_DIR)/udiskslvm2moduleiface.c            \
	$(LVM2_MODULE_DIR)/udiskslinuxlogicalvolume.h         \
	$(LVM2_MODULE_DIR)/udiskslinuxlogicalvolume.c         \
	$(LVM2_MODULE_DIR)/udiskslinuxlogicalvolumeobject.h   \
	$(LVM2_MODULE_DIR)/udiskslinuxlogicalvolumeobject.c   \
	$(LVM2_MODULE_DIR)/udiskslinuxphysicalvolume.h        \
	$(LVM2_MODULE_DIR)/udiskslinuxphysicalvolume.c        \
	$(LVM2_MODULE_DIR)/udiskslinuxvolumegroup.h           \
	$(LVM2_MODULE_DIR)/udiskslinuxvolumegroup.c           \
	$(LVM2_MODULE_DIR)/udiskslinuxvolumegroupobject.h     \
	$(LVM2_MODULE_DIR)/udiskslinuxvolumegroupobject.c     \
	$(LVM2_MODULE_DIR)/udiskslinuxblocklvm2.h             \
	$(LVM2_MODULE_DIR)/udiskslinuxblocklvm2.c             \
	$(LVM2_MODULE_DIR)/udiskslvm2daemonutil.h             \
	$(LVM2_MODULE_DIR)/udiskslvm2daemonutil.c             \
	$(LVM2_MODULE_DIR)/udiskslvm2dbusutil.h               \
	$(LVM2_MODULE_DIR)/udiskslvm2dbusutil.c               \
	$(LVM2_MODULE_DIR)/udiskslvm2util.h                   \
	$(LVM2_MODULE_DIR)/udiskslvm2util.c                   \
	$(LVM2_MODULE_DIR)/udiskslinuxmanagerlvm2.h           \
	$(LVM2_MODULE_DIR)/udiskslinuxmanagerlvm2.c           \
	$(LVM2_MODULE_DIR)/udiskslvm2state.h                  \
	$(LVM2_MODULE_DIR)/udiskslvm2state.c                  \
	$(LVM2_MODULE_DIR)/jobhelpers.h                       \
	$(LVM2_MODULE_DIR)/jobhelpers.c                       \
	$(NULL)

# ------------------------------------------------------------------------

BUILT_SOURCES =                                           \
	$(UDISKS_DIR)/udisksenumtypes.h                       \
	$(UDISKS_DIR)/udisksenumtypes.c                       \
	$(UDISKS_DIR)/udisks-generated.h                      \
	$(UDISKS_DIR)/udisks-generated.c                      \
	$(UDISKS_DIR)/udisksversion.h                         \
	$(NULL)

libudisks2_la_SOURCES =                                   \
	$(BUILT_SOURCES)                                      \
	$(UDISKS_DIR)/udisksclient.h                          \
	$(UDISKS_DIR)/udisksclient.c                          \
	$(UDISKS_DIR)/udisksobjectinfo.h                      \
	$(UDISKS_DIR)/udisksobjectinfo.c                      \
	$(UDISKS_DIR)/udisksenums.h                           \
	$(UDISKS_DIR)/udiskserror.h                           \
	$(UDISKS_DIR)/udiskserror.c                           \
	$(UDISKS_DIR)/udiskstypes.h                           \
	$(NULL)

libudisks2_la_cflags =                                    \
	$(GLIB_CFLAGS)                                        \
	$(GIO_CFLAGS)                                         \
	$(NULL)

if HAVE_INTROSPECTION

INTROSPECTION_GIRS = UDisks-2.0.gir

girdir = $(INTROSPECTION_GIRDIR)
gir_DATA = UDisks-2.0.gir

typelibdir = $(INTROSPECTION_TYPELIBDIR)
typelib_DATA = UDisks-2.0.typelib

UDisks_2_0_gir_SCANNERFLAGS =                             \
	--c-include='udisks/udisks.h'                         \
	--warn-all                                            \
	--namespace UDisks                                    \
	--identifier-prefix UDisks                            \
	--symbol-prefix udisks                                \
	--accept-unprefixed	                              \
	--warn-all                                            \
	$(NULL)

UDisks_2_0_gir_CFLAGS =                                   \
	$(libudisks2_la_cflags)                               \
	-DUDISKS_COMPILATION                                  \
	-I ../src                                       \
	-I ..                                     \
	$(NULL)

UDisks-2.0.gir:	../src/libudisks-daemon.la $(UDISKS_DIR)/libudisks2.la $(LVM2_MODULE_DIR)/libudisks2_lvm2.la
UDisks_2_0_gir_INCLUDES = \
	Gio-2.0           \
	GUdev-1.0         \
	Polkit-1.0        \
	$(NULL)
UDisks_2_0_gir_LIBS =                                     \
	blockdev                         \
	../src/libudisks-daemon.la                            \
	$(UDISKS_DIR)/libudisks2.la                           \
	$(LVM2_MODULE_DIR)/libudisks2_lvm2.la                 \
	$(NULL)
UDisks_2_0_gir_FILES =                                    \
	$(libudisks2_la_SOURCES)                              \
	$(lvm2_SOURCES)                                       \
	$(NULL)
UDisks_2_0_gir_EXPORT_PACKAGES = udisks2

include $(INTROSPECTION_MAKEFILE)

endif # HAVE_INTROSPECTION

CLEANFILES =

