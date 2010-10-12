## Process this file with automake to produce Makefile.in

NULL =

INCLUDES = \
	-I$(top_builddir)					\
	-I$(top_srcdir)	 					\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	-DUDISKS_API_IS_SUBJECT_TO_CHANGE			\
	$(NULL)

bin_PROGRAMS = udisks

udisks_SOURCES = 					\
				udisks.c		\
	$(NULL)

udisks_CFLAGS = 					\
	$(GLIB_CFLAGS) 					\
	$(GIO_CFLAGS)					\
	$(NULL)

udisks_LDADD = 						\
	$(GLIB_LIBS) 					\
	$(GIO_LIBS)					\
	$(top_builddir)/udisks/libudisks-2.0.la		\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

#slashsbin_PROGRAMS = umount.udisks
#umount_udisks_SOURCES = umount-udisks.c
#umount_udisks_LDADD = $(DBUS_GLIB_LIBS) $(POLKIT_DBUS_LIBS)

# ----------------------------------------------------------------------------------------------------

#if REMOTE_ACCESS_ENABLED
#bin_PROGRAMS += udisks-tcp-bridge
#endif # REMOTE_ACCESS_ENABLED
#udisks_tcp_bridge_SOURCES = udisks-tcp-bridge.c
#udisks_tcp_bridge_LDADD = $(DBUS_GLIB_LIBS)

# ----------------------------------------------------------------------------------------------------

profiledir = $(sysconfdir)/profile.d
profile_SCRIPTS = udisks-bash-completion.sh

EXTRA_DIST = $(profile_SCRIPTS)

clean-local :
	rm -f *~ $(service_DATA) $(dbusconf_DATA)
