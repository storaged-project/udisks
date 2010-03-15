
NULL =

INCLUDES = \
	-I$(top_builddir)/src -I$(top_srcdir)/src \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT	\
	$(DBUS_GLIB_CFLAGS) \
	$(POLKIT_GOBJECT_1_CFLAGS) \
	$(GUDEV_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS)

# TODO: ideally move most of this to udev and/or util-linux
#

udevhelperdir = $(slashlibdir)/udev
udevhelper_PROGRAMS =	udisks-part-id 			\
			udisks-dm-export 		\
		      	udisks-probe-ata-smart 		\
			udisks-probe-sas-expander 	\
			$(NULL)

if HAVE_LVM2
udevhelper_PROGRAMS += udisks-lvm-pv-export
endif

udisks_part_id_SOURCES = part-id.c
udisks_part_id_CPPFLAGS = $(AM_CPPFLAGS) $(LIBUDEV_CFLAGS)
udisks_part_id_LDADD = $(GLIB_LIBS) $(LIBUDEV_LIBS) $(top_builddir)/src/helpers/libpartutil.la

udisks_dm_export_SOURCES = udisks-dm-export.c
udisks_dm_export_CPPFLAGS = $(AM_CPPFLAGS) $(DEVMAPPER_CFLAGS) $(GLIB_CFLAGS)
udisks_dm_export_LDADD = $(DEVMAPPER_LIBS) $(GLIB_LIBS)

udisks_lvm_pv_export_SOURCES = udisks-lvm-pv-export.c
udisks_lvm_pv_export_CPPFLAGS = $(AM_CPPFLAGS) $(DEVMAPPER_CFLAGS) $(LVM2_CFLAGS) $(GLIB_CFLAGS)
udisks_lvm_pv_export_LDADD = $(DEVMAPPER_LIBS) $(LVM2_LIBS) $(GLIB_LIBS)

udisks_probe_ata_smart_SOURCES = udisks-probe-ata-smart.c
udisks_probe_ata_smart_CPPFLAGS = $(AM_CPPFLAGS) $(LIBATASMART_CFLAGS)
udisks_probe_ata_smart_LDADD = $(LIBATASMART_LIBS)

udisks_probe_sas_expander_SOURCES = udisks-probe-sas-expander.c
udisks_probe_sas_expander_CPPFLAGS = $(AM_CPPFLAGS) $(GLIB_CFLAGS)
udisks_probe_sas_expander_LDADD = $(GLIB_LIBS)
