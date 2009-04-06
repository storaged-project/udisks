## Process this file with automake to produce Makefile.in

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
	$(POLKIT_DBUS_CFLAGS) \
	$(DEVKIT_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS)

BUILT_SOURCES =							\
	devkit-disks-daemon-glue.h				\
	devkit-disks-device-glue.h				\
	devkit-disks-marshal.h		devkit-disks-marshal.c

devkit-disks-marshal.h: devkit-disks-marshal.list
	glib-genmarshal $< --prefix=devkit_disks_marshal --header > $@

devkit-disks-marshal.c: devkit-disks-marshal.list
	echo "#include \"devkit-disks-marshal.h\"" > $@ && glib-genmarshal $< --prefix=devkit_disks_marshal --body >> $@

devkit-disks-daemon-glue.h: org.freedesktop.DeviceKit.Disks.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-server --output=devkit-disks-daemon-glue.h org.freedesktop.DeviceKit.Disks.xml

devkit-disks-device-glue.h: org.freedesktop.DeviceKit.Disks.Device.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_device --mode=glib-server --output=devkit-disks-device-glue.h org.freedesktop.DeviceKit.Disks.Device.xml

libexec_PROGRAMS = devkit-disks-daemon

dbusifdir = $(datadir)/dbus-1/interfaces
dbusif_DATA = org.freedesktop.DeviceKit.Disks.xml org.freedesktop.DeviceKit.Disks.Device.xml

devkit_disks_daemon_SOURCES = 						\
	devkit-disks-types.h						\
	devkit-disks-private.h						\
	devkit-disks-daemon.h		devkit-disks-daemon.c		\
	devkit-disks-device.h		devkit-disks-device.c		\
	devkit-disks-device-private.h	devkit-disks-device-private.c	\
	devkit-disks-mount-file.h	devkit-disks-mount-file.c	\
	devkit-disks-mount.h		devkit-disks-mount.c		\
	devkit-disks-mount-monitor.h	devkit-disks-mount-monitor.c	\
	devkit-disks-inhibitor.h	devkit-disks-inhibitor.c	\
	devkit-disks-ata-smart-db.h	devkit-disks-ata-smart-db.c	\
	devkit-disks-poller.h		devkit-disks-poller.c		\
	main.c								\
	$(BUILT_SOURCES)

devkit_disks_daemon_CPPFLAGS = 				\
	-I$(top_srcdir)/src				\
	-DG_LOG_DOMAIN=\"devkit-disks-daemon\"		\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)					\
	$(NULL)

devkit_disks_daemon_CFLAGS = 				\
	$(ZLIB_CFLAGS)					\
	$(SQLITE3_CFLAGS)				\
	$(LIBATASMART_CFLAGS)				\
	$(NULL)

devkit_disks_daemon_LDADD = 				\
	$(GIO_LIBS)					\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)				\
	$(DEVKIT_LIBS)					\
	$(ZLIB_LIBS)					\
	$(SQLITE3_LIBS)					\
	$(LIBATASMART_LIBS)				\
	$(NULL)

noinst_LTLIBRARIES = libpartutil.la
libpartutil_la_SOURCES = partutil.h partutil.c
libpartutil_la_CPPFLAGS = $(LIBPARTED_CFLAGS)
libpartutil_la_LIBADD = $(LIBPARTED_LIBS)

libexec_PROGRAMS += devkit-disks-helper-mkfs                    	\
		    devkit-disks-helper-delete-partition        	\
		    devkit-disks-helper-create-partition        	\
		    devkit-disks-helper-modify-partition        	\
		    devkit-disks-helper-create-partition-table		\
		    devkit-disks-helper-change-filesystem-label 	\
		    devkit-disks-helper-linux-md-remove-component	\
		    devkit-disks-helper-fstab-mounter			\
		    devkit-disks-helper-ata-smart-collect		\
		    devkit-disks-helper-ata-smart-selftest		\
		    $(NULL)

libexec_SCRIPTS = devkit-disks-helper-change-luks-password

devkit_disks_helper_mkfs_SOURCES = job-shared.h job-mkfs.c
devkit_disks_helper_mkfs_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_mkfs_LDADD = $(GLIB_LIBS)

devkit_disks_helper_delete_partition_SOURCES = job-shared.h job-delete-partition.c
devkit_disks_helper_delete_partition_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_delete_partition_LDADD = $(GLIB_LIBS) libpartutil.la

devkit_disks_helper_create_partition_SOURCES = job-shared.h job-create-partition.c
devkit_disks_helper_create_partition_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_create_partition_LDADD = $(GLIB_LIBS) libpartutil.la

devkit_disks_helper_modify_partition_SOURCES = job-shared.h job-modify-partition.c
devkit_disks_helper_modify_partition_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_modify_partition_LDADD = $(GLIB_LIBS) libpartutil.la

devkit_disks_helper_create_partition_table_SOURCES = job-shared.h job-create-partition-table.c
devkit_disks_helper_create_partition_table_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_create_partition_table_LDADD = $(GLIB_LIBS) libpartutil.la

devkit_disks_helper_change_filesystem_label_SOURCES = job-shared.h job-change-filesystem-label.c
devkit_disks_helper_change_filesystem_label_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_change_filesystem_label_LDADD = $(GLIB_LIBS)

devkit_disks_helper_ata_smart_selftest_SOURCES = job-shared.h job-ata-smart-selftest.c
devkit_disks_helper_ata_smart_selftest_CPPFLAGS = $(AM_CPPFLAGS) $(LIBATASMART_CFLAGS) $(GLIB_CFLAGS)
devkit_disks_helper_ata_smart_selftest_LDADD = $(LIBATASMART_LIBS) $(GLIB_LIBS)

devkit_disks_helper_ata_smart_collect_SOURCES = job-ata-smart-collect.c
devkit_disks_helper_ata_smart_collect_CPPFLAGS = $(AM_CPPFLAGS) $(LIBATASMART_CFLAGS) $(GLIB_CFLAGS)
devkit_disks_helper_ata_smart_collect_LDADD = $(LIBATASMART_LIBS) $(GLIB_LIBS)

devkit_disks_helper_linux_md_remove_component_SOURCES = job-shared.h job-linux-md-remove-component.c
devkit_disks_helper_linux_md_remove_component_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_linux_md_remove_component_LDADD = $(GLIB_LIBS)

devkit_disks_helper_fstab_mounter_SOURCES = job-shared.h job-fstab-mounter.c
devkit_disks_helper_fstab_mounter_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_fstab_mounter_LDADD = $(GLIB_LIBS)

# TODO: move to udev
udevhelperdir = $(slashlibdir)/udev
udevhelper_PROGRAMS = devkit-disks-part-id devkit-disks-dm-export devkit-disks-probe-ata-smart

devkit_disks_part_id_SOURCES = part-id.c
devkit_disks_part_id_CPPFLAGS = $(AM_CPPFLAGS) $(LIBUDEV_CFLAGS)
devkit_disks_part_id_LDADD = $(GLIB_LIBS) $(LIBUDEV_LIBS) libpartutil.la

devkit_disks_dm_export_SOURCES = devkit-disks-dm-export.c
devkit_disks_dm_export_CPPFLAGS = $(AM_CPPFLAGS) $(DEVMAPPER_CFLAGS)
devkit_disks_dm_export_LDADD = $(DEVMAPPER_LIBS)

devkit_disks_probe_ata_smart_SOURCES = devkit-disks-probe-ata-smart.c
devkit_disks_probe_ata_smart_CPPFLAGS = $(AM_CPPFLAGS) $(LIBATASMART_CFLAGS)
devkit_disks_probe_ata_smart_LDADD = $(LIBATASMART_LIBS)
# end move to udev


servicedir       = $(datadir)/dbus-1/system-services
service_in_files = org.freedesktop.DeviceKit.Disks.service.in
service_DATA     = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dbusconf_in_files = org.freedesktop.DeviceKit.Disks.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

$(dbusconf_DATA): $(dbusconf_in_files) Makefile
	cp $< $@

udevrulesdir = $(sysconfdir)/udev/rules.d
udevrules_DATA = 95-devkit-disks.rules

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = org.freedesktop.DeviceKit.Disks.xml        \
	     org.freedesktop.DeviceKit.Disks.Device.xml \
	     devkit-disks-marshal.list			\
	     95-devkit-disks.rules                      \
	     devkit-disks-helper-change-luks-password	\
	     $(service_in_files)                        \
	     $(dbusconf_in_files)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)

install-data-local:
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/lib/DeviceKit-disks
	-chmod 0700 $(DESTDIR)$(localstatedir)/lib/DeviceKit-disks
