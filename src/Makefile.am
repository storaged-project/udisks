## Process this file with automake to produce Makefile.in

SUBDIRS =

NULL =

INCLUDES = \
	-I$(top_builddir)/src -I$(top_srcdir)/src 		\
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\" 		\
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\" 		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" 			\
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" 			\
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\" 	\
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\" 		\
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" 			\
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT			\
	-DUDISKS_API_IS_SUBJECT_TO_CHANGE			\
	$(POLKIT_GOBJECT_1_CFLAGS) 				\
	$(GUDEV_CFLAGS) 					\
	$(GLIB_CFLAGS) 						\
	$(GIO_CFLAGS)						\
	$(NULL)

libexec_PROGRAMS = udisks-daemon

udisks_daemon_SOURCES = 						\
					main.c				\
	gposixsignal.h			gposixsignal.c			\
	types.h								\
	linuxblock.h			linuxblock.c			\
	$(NULL)

udisks_daemon_CPPFLAGS = 				\
	-I$(top_srcdir)					\
	-DG_LOG_DOMAIN=\"udisks-daemon\"		\
	$(AM_CPPFLAGS)					\
	$(NULL)

udisks_daemon_CFLAGS = 					\
	$(LIBATASMART_CFLAGS)				\
	$(NULL)

udisks_daemon_LDADD = 					\
	$(GIO_LIBS)					\
	$(POLKIT_GOBJECT_1_LIBS) 			\
	$(GUDEV_LIBS)					\
	$(LIBATASMART_LIBS)				\
	$(top_builddir)/udisks/libudisks.la		\
	$(NULL)

# ----------------------------------------------------------------------------------------------------

install-data-local:
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/lib/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/lib/udisks
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/run/udisks
	-chmod 0700 $(DESTDIR)$(localstatedir)/run/udisks
