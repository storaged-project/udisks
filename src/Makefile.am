## Process this file with automake to produce Makefile.in

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
	devkit-disks-daemon.h		devkit-disks-daemon.c		\
	devkit-disks-logger.h		devkit-disks-logger.c		\
	devkit-disks-device.h		devkit-disks-device.c		\
	devkit-disks-device-private.h	devkit-disks-device-private.c	\
	mounts-file.h			mounts-file.c			\
	devkit-disks-mount-monitor.h	devkit-disks-mount-monitor.c	\
	devkit-disks-inhibitor.h	devkit-disks-inhibitor.c	\
	poller.h			poller.c			\
	main.c								\
	$(BUILT_SOURCES)

devkit_disks_daemon_CPPFLAGS = 				\
	-I$(top_srcdir)/src				\
	-DG_LOG_DOMAIN=\"devkit-disks-daemon\"		\
	$(SQLITE3_CFLAGS)				\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

devkit_disks_daemon_LDADD = 				\
	$(GIO_LIBS)					\
	$(SQLITE3_LIBS)					\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)				\
	$(DEVKIT_LIBS)

noinst_LTLIBRARIES = libpartutil.la
libpartutil_la_SOURCES = partutil.h partutil.c
libpartutil_la_CPPFLAGS = $(LIBPARTED_CFLAGS)
libpartutil_la_LIBADD = $(LIBPARTED_LIBS)

libexec_PROGRAMS += devkit-disks-helper-erase                   	\
		    devkit-disks-helper-mkfs                    	\
		    devkit-disks-helper-delete-partition        	\
		    devkit-disks-helper-create-partition        	\
		    devkit-disks-helper-modify-partition        	\
		    devkit-disks-helper-create-partition-table		\
		    devkit-disks-helper-change-filesystem-label 	\
		    devkit-disks-helper-smart-selftest			\
		    devkit-disks-helper-linux-md-remove-component	\
		    devkit-disks-helper-fstab-mounter

libexec_SCRIPTS = devkit-disks-helper-change-luks-password

devkit_disks_helper_erase_SOURCES = job-shared.h job-erase.c
devkit_disks_helper_erase_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_erase_LDADD = $(GLIB_LIBS)

devkit_disks_helper_mkfs_SOURCES = job-shared.h job-mkfs.c
devkit_disks_helper_mkfs_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_mkfs_LDADD = $(GLIB_LIBS)

devkit_disks_helper_delete_partition_SOURCES = job-shared.h job-delete-partition.c
devkit_disks_helper_delete_partition_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_delete_partition_LDADD = $(GLIB_LIBS) $(top_builddir)/src/libpartutil.la

devkit_disks_helper_create_partition_SOURCES = job-shared.h job-create-partition.c
devkit_disks_helper_create_partition_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_create_partition_LDADD = $(GLIB_LIBS) $(top_builddir)/src/libpartutil.la

devkit_disks_helper_modify_partition_SOURCES = job-shared.h job-modify-partition.c
devkit_disks_helper_modify_partition_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_modify_partition_LDADD = $(GLIB_LIBS) $(top_builddir)/src/libpartutil.la

devkit_disks_helper_create_partition_table_SOURCES = job-shared.h job-create-partition-table.c
devkit_disks_helper_create_partition_table_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_create_partition_table_LDADD = $(GLIB_LIBS) $(top_builddir)/src/libpartutil.la

devkit_disks_helper_change_filesystem_label_SOURCES = job-shared.h job-change-filesystem-label.c
devkit_disks_helper_change_filesystem_label_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_change_filesystem_label_LDADD = $(GLIB_LIBS)

devkit_disks_helper_smart_selftest_SOURCES = job-shared.h job-smart-selftest.c
devkit_disks_helper_smart_selftest_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_smart_selftest_LDADD = $(GLIB_LIBS)

devkit_disks_helper_linux_md_remove_component_SOURCES = job-shared.h job-linux-md-remove-component.c
devkit_disks_helper_linux_md_remove_component_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_linux_md_remove_component_LDADD = $(GLIB_LIBS)

devkit_disks_helper_fstab_mounter_SOURCES = job-shared.h job-fstab-mounter.c
devkit_disks_helper_fstab_mounter_CPPFLAGS = $(AM_CPPFLAGS)
devkit_disks_helper_fstab_mounter_LDADD = $(GLIB_LIBS)

# TODO: move to udev
udevhelperdir = $(slashlibdir)/udev
udevhelper_PROGRAMS = part_id devkit-disks-dm-export

part_id_SOURCES = part-id.c
part_id_CPPFLAGS = $(AM_CPPFLAGS)
part_id_LDADD = $(GLIB_LIBS) $(top_builddir)/src/libpartutil.la

devkit_disks_dm_export_SOURCES = devkit-disks-dm-export.c
devkit_disks_dm_export_CPPFLAGS = $(AM_CPPFLAGS) $(DEVMAPPER_CFLAGS)
devkit_disks_dm_export_LDADD = $(DEVMAPPER_LIBS)
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
	-chmod 0755 $(DESTDIR)$(localstatedir)/lib/DeviceKit-disks
