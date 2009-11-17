
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

libexec_PROGRAMS = 					\
	udisks-helper-mkfs                    		\
	udisks-helper-delete-partition        		\
	udisks-helper-create-partition        		\
	udisks-helper-modify-partition        		\
	udisks-helper-create-partition-table		\
	udisks-helper-change-filesystem-label 		\
	udisks-helper-linux-md-remove-component		\
	udisks-helper-fstab-mounter			\
	udisks-helper-ata-smart-collect			\
	udisks-helper-ata-smart-selftest		\
	udisks-helper-drive-detach			\
	udisks-helper-drive-poll			\
	udisks-helper-linux-md-check			\
	udisks-helper-drive-benchmark			\
	$(NULL)

noinst_LTLIBRARIES = libpartutil.la
libpartutil_la_SOURCES = partutil.h partutil.c
libpartutil_la_CPPFLAGS = $(LIBPARTED_CFLAGS)
libpartutil_la_LIBADD = $(LIBPARTED_LIBS)

dist_libexec_SCRIPTS = 					\
	udisks-helper-change-luks-password 		\
	udisks-helper-mdadm-expand	 		\
	$(NULL)

udisks_helper_mkfs_SOURCES = job-shared.h job-mkfs.c
udisks_helper_mkfs_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_mkfs_LDADD = $(GLIB_LIBS)

udisks_helper_delete_partition_SOURCES = job-shared.h job-delete-partition.c
udisks_helper_delete_partition_CPPFLAGS = $(AM_CPPFLAGS)
udisks_helper_delete_partition_LDADD = $(GLIB_LIBS) libpartutil.la

udisks_helper_create_partition_SOURCES = job-shared.h job-create-partition.c
udisks_helper_create_partition_CPPFLAGS = $(AM_CPPFLAGS) $(GUDEV_CFLAGS)
udisks_helper_create_partition_LDADD = $(GLIB_LIBS) $(GUDEV_LIBS)  libpartutil.la

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

