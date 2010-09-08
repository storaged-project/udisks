## Process this file with automake to produce Makefile.in

SUBDIRS =

NULL =

EXTRA_DIST =

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

# ----------------------------------------------------------------------------------------------------

noinst_PROGRAMS = udisks-test

udisks_test_SOURCES = 					\
				test.c			\
	testutil.h		testutil.c		\
	$(NULL)

udisks_test_CFLAGS = 					\
	-DG_LOG_DOMAIN=\"udisks-test\"			\
	$(LIBATASMART_CFLAGS)				\
	$(NULL)

udisks_test_LDADD = 					\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(top_builddir)/src/libudisks-daemon.la		\
	$(NULL)

