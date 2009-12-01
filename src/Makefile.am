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
	$(POLKIT_GOBJECT_1_CFLAGS) \
	$(GUDEV_CFLAGS) -DG_UDEV_API_IS_SUBJECT_TO_CHANGE \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS)

BUILT_SOURCES =							\
	devkit-disks-daemon-glue.h				\
	devkit-disks-device-glue.h				\
	devkit-disks-adapter-glue.h				\
	devkit-disks-port-glue.h				\
	devkit-disks-expander-glue.h				\
	devkit-disks-marshal.h		devkit-disks-marshal.c

devkit-disks-marshal.h: udisks-marshal.list
	glib-genmarshal $< --prefix=devkit_disks_marshal --header > $@

devkit-disks-marshal.c: udisks-marshal.list
	echo "#include \"devkit-disks-marshal.h\"" > $@ && glib-genmarshal $< --prefix=devkit_disks_marshal --body >> $@

devkit-disks-daemon-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-server --output=devkit-disks-daemon-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.xml

devkit-disks-device-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_device --mode=glib-server --output=devkit-disks-device-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Device.xml

devkit-disks-adapter-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Adapter.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_adapter --mode=glib-server --output=devkit-disks-adapter-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Adapter.xml

devkit-disks-expander-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Expander.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_expander --mode=glib-server --output=devkit-disks-expander-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Expander.xml

devkit-disks-port-glue.h: $(top_srcdir)/data/org.freedesktop.UDisks.Port.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_port --mode=glib-server --output=devkit-disks-port-glue.h $(top_srcdir)/data/org.freedesktop.UDisks.Port.xml

libexec_PROGRAMS = udisks-daemon

udisks_daemon_SOURCES = 								\
	devkit-disks-types.h								\
	devkit-disks-private.h								\
	devkit-disks-daemon.h			devkit-disks-daemon.c			\
	devkit-disks-device.h			devkit-disks-device.c			\
	devkit-disks-device-private.h		devkit-disks-device-private.c		\
	devkit-disks-adapter.h			devkit-disks-adapter.c			\
	devkit-disks-adapter-private.h		devkit-disks-adapter-private.c		\
	devkit-disks-expander.h			devkit-disks-expander.c			\
	devkit-disks-expander-private.h		devkit-disks-expander-private.c		\
	devkit-disks-port.h			devkit-disks-port.c			\
	devkit-disks-port-private.h		devkit-disks-port-private.c		\
	devkit-disks-mount-file.h		devkit-disks-mount-file.c		\
	devkit-disks-mount.h			devkit-disks-mount.c			\
	devkit-disks-mount-monitor.h		devkit-disks-mount-monitor.c		\
	devkit-disks-inhibitor.h		devkit-disks-inhibitor.c		\
	devkit-disks-poller.h			devkit-disks-poller.c			\
	main.c										\
	$(BUILT_SOURCES)

udisks_daemon_CPPFLAGS = 				\
	-I$(top_srcdir)/src				\
	-DG_LOG_DOMAIN=\"devkit-disks-daemon\"		\
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)					\
	$(NULL)

udisks_daemon_CFLAGS = 					\
	$(LIBATASMART_CFLAGS)				\
	$(NULL)

udisks_daemon_LDADD = 					\
	$(GIO_LIBS)					\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_GOBJECT_1_LIBS) 			\
	$(GUDEV_LIBS)					\
	$(LIBATASMART_LIBS)				\
	$(NULL)

noinst_LTLIBRARIES = libpartutil.la
libpartutil_la_SOURCES = partutil.h partutil.c
libpartutil_la_CPPFLAGS = $(LIBPARTED_CFLAGS)
libpartutil_la_LIBADD = $(LIBPARTED_LIBS)

libexec_PROGRAMS += udisks-helper-mkfs                    		\
		    udisks-helper-delete-partition        		\
		    udisks-helper-create-partition        		\
		    udisks-helper-modify-partition        		\
		    udisks-helper-create-partition-table		\
		    udisks-helper-change-filesystem-label 		\
		    udisks-helper-linux-md-remove-component		\
		    udisks-helper-fstab-mounter				\
		    udisks-helper-ata-smart-collect			\
		    udisks-helper-ata-smart-selftest			\
		    udisks-helper-drive-detach				\
		    udisks-helper-drive-poll				\
		    udisks-helper-linux-md-check			\
		    udisks-helper-drive-benchmark			\
		    $(NULL)

libexec_SCRIPTS = udisks-helper-change-luks-password

udisks_helper_mkfs_SOURCES = job-shared.h job-mkfs.c
udisks_helper_mkfs_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_mkfs_LDADD = $(GLIB_LIBS)

udisks_helper_delete_partition_SOURCES = job-shared.h job-delete-partition.c
udisks_helper_delete_partition_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_delete_partition_LDADD = $(GLIB_LIBS) libpartutil.la

udisks_helper_create_partition_SOURCES = job-shared.h job-create-partition.c
udisks_helper_create_partition_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_create_partition_LDADD = $(GLIB_LIBS) libpartutil.la

udisks_helper_modify_partition_SOURCES = job-shared.h job-modify-partition.c
udisks_helper_modify_partition_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_modify_partition_LDADD = $(GLIB_LIBS) libpartutil.la

udisks_helper_create_partition_table_SOURCES = job-shared.h job-create-partition-table.c
udisks_helper_create_partition_table_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_create_partition_table_LDADD = $(GLIB_LIBS) libpartutil.la

udisks_helper_change_filesystem_label_SOURCES = job-shared.h job-change-filesystem-label.c
udisks_helper_change_filesystem_label_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_change_filesystem_label_LDADD = $(GLIB_LIBS)

udisks_helper_ata_smart_selftest_SOURCES = job-shared.h job-ata-smart-selftest.c
udisks_helper_ata_smart_selftest_CPPFLAGS = $(AM_CPPFLAGS) $(LIBATASMART_CFLAGS) $(GLIB_CFLAGS)
udisks_helper_ata_smart_selftest_LDADD = $(LIBATASMART_LIBS) $(GLIB_LIBS)

udisks_helper_ata_smart_collect_SOURCES = job-ata-smart-collect.c
udisks_helper_ata_smart_collect_CPPFLAGS = $(AM_CPPFLAGS) $(LIBATASMART_CFLAGS) $(GLIB_CFLAGS)
udisks_helper_ata_smart_collect_LDADD = $(LIBATASMART_LIBS) $(GLIB_LIBS)

udisks_helper_linux_md_remove_component_SOURCES = job-shared.h job-linux-md-remove-component.c
udisks_helper_linux_md_remove_component_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_linux_md_remove_component_LDADD = $(GLIB_LIBS)

udisks_helper_fstab_mounter_SOURCES = job-shared.h job-fstab-mounter.c
udisks_helper_fstab_mounter_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_fstab_mounter_LDADD = $(GLIB_LIBS)

udisks_helper_drive_detach_SOURCES = job-shared.h job-drive-detach.c
udisks_helper_drive_detach_CPPFLAGS = $(AM_CPPFLAGS) $(LIBUDEV_CFLAGS) $(SGUTILS_CFLAGS)
udisks_helper_drive_detach_LDADD = $(LIBUDEV_LIBS) $(SGUTILS_LIBS) $(GLIB_LIBS)

udisks_helper_linux_md_check_SOURCES = job-shared.h job-linux-md-check.c
udisks_helper_linux_md_check_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_linux_md_check_LDADD = $(GLIB_LIBS)

udisks_helper_drive_poll_SOURCES = job-shared.h job-drive-poll.c
udisks_helper_drive_poll_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_drive_poll_LDADD =  $(GLIB_LIBS)

udisks_helper_drive_benchmark_SOURCES = job-drive-benchmark.c
udisks_helper_drive_benchmark_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_drive_benchmark_LDADD =  $(GLIB_LIBS)

# TODO: move to udev
udevhelperdir = $(slashlibdir)/udev
udevhelper_PROGRAMS = udisks-part-id udisks-dm-export udisks-probe-ata-smart udisks-probe-sas-expander

udisks_part_id_SOURCES = part-id.c
udisks_part_id_CPPFLAGS = $(AM_CPPFLAGS) $(LIBUDEV_CFLAGS)
udisks_part_id_LDADD = $(GLIB_LIBS) $(LIBUDEV_LIBS) libpartutil.la

udisks_dm_export_SOURCES = devkit-disks-dm-export.c
udisks_dm_export_CPPFLAGS = $(AM_CPPFLAGS) $(DEVMAPPER_CFLAGS)
udisks_dm_export_LDADD = $(DEVMAPPER_LIBS)

udisks_probe_ata_smart_SOURCES = devkit-disks-probe-ata-smart.c
udisks_probe_ata_smart_CPPFLAGS = $(AM_CPPFLAGS) $(LIBATASMART_CFLAGS)
udisks_probe_ata_smart_LDADD = $(LIBATASMART_LIBS)

udisks_probe_sas_expander_SOURCES = devkit-disks-probe-sas-expander.c
udisks_probe_sas_expander_CPPFLAGS = $(AM_CPPFLAGS) $(GLIB_CFLAGS)
udisks_probe_sas_expander_LDADD = $(GLIB_LIBS)
# end move to udev

# ----------------------------------------------------------------------------------------------------

polkitmodulesdir = $(libdir)/polkit-1/extensions
polkitmodules_LTLIBRARIES = libudisks-action-lookup.la

libudisks_action_lookup_la_SOURCES =                    		\
	devkit-disks-polkit-action-lookup.c                             \
        $(NULL)

libudisks_action_lookup_la_CFLAGS =                        		\
        -DPOLKIT_BACKEND_I_KNOW_API_IS_SUBJECT_TO_CHANGE                \
        -DG_LOG_DOMAIN=\"UDisks-Action-Lookup\"            		\
	$(POLKIT_BACKEND_1_CFLAGS) 					\
        $(NULL)

libudisks_action_lookup_la_LDFLAGS =                       		\
	-export_dynamic -avoid-version -module -no-undefined 		\
	-export-symbols-regex '^g_io_module_(load|unload)' 		\
	$(POLKIT_BACKEND_1_LIBS) 					\
        $(NULL)

libudisks_action_lookup_la_LIBADD =                 			\
        $(NULL)


# ----------------------------------------------------------------------------------------------------

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = 						\
	devkit-disks-marshal.list			\
	udisks-helper-change-luks-password		\
	$(NULL)

install-data-local:
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/lib/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/lib/udisks
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/run/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/run/udisks
