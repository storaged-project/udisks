## Process this file with automake to produce Makefile.in

SUBDIRS =

NULL =

EXTRA_DIST =                                                                   \
	test_polkitd.py                                                        \
	integration-test                                                       \
	$(NULL)

AM_CPPFLAGS = \
	-I$(top_builddir) -I$(top_srcdir)                                      \
	-I$(top_builddir)/src -I$(top_srcdir)/src                              \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"                              \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"                              \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"                                    \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\"                                      \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\"                        \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\"                                \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\"                                      \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                                \
	-DSTORAGED_COMPILATION                                                 \
	-DSTORAGED_TEST_DIR=\""$(builddir)"\"                                  \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GUDEV_CFLAGS)                                                        \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

# ------------------------------------------------------------------------------

TESTS = storaged-test

noinst_PROGRAMS = storaged-test storaged-test-helper

storaged_test_SOURCES =                                                        \
	test.c                                                                 \
	testutil.h    testutil.c                                               \
	$(NULL)

storaged_test_CFLAGS =                                                         \
	-DG_LOG_DOMAIN=\"storaged-test\"                                       \
	$(NULL)

storaged_test_LDADD =                                                          \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(top_builddir)/src/libstoraged-daemon.la                              \
	$(NULL)

storaged_test_helper_SOURCES =                                                 \
	helper.c                                                               \
	testutil.h    testutil.c                                               \
	$(NULL)

storaged_test_helper_CFLAGS =                                                  \
	-DG_LOG_DOMAIN=\"storaged-test-helper\"                                \
	$(NULL)

storaged_test_helper_LDADD =                                                   \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(NULL)
