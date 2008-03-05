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
	@GLIB_CFLAGS@

BUILT_SOURCES =						\
	devkit-disks-client-glue.h

devkit-disks-client-glue.h: $(top_srcdir)/src/org.freedesktop.DeviceKit.Disks.xml Makefile.am
	dbus-binding-tool --prefix=devkit_disks_daemon --mode=glib-client --output=devkit-disks-client-glue.h $(top_srcdir)/src/org.freedesktop.DeviceKit.Disks.xml

bin_PROGRAMS = devkit-disks

devkit_disks_SOURCES = 					\
				devkit-disks.c		\
	$(BUILT_SOURCES)

devkit_disks_CPPFLAGS = 				\
	-DG_LOG_DOMAIN=\"devkit-disks\"		        \
	$(DISABLE_DEPRECATED)				\
	$(AM_CPPFLAGS)

devkit_disks_LDADD = 					\
	$(DBUS_GLIB_LIBS)				\
	$(POLKIT_DBUS_LIBS)

CLEANFILES = $(BUILT_SOURCES)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)
