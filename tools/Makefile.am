## Process this file with automake to produce Makefile.in

NULL =

AM_CPPFLAGS =                                                                  \
	-I$(top_builddir) -I$(top_srcdir)                                      \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"                              \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"                              \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"                                    \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\"                                      \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\"                        \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\"                                \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\"                                      \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                                \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

bin_PROGRAMS = storagedctl

storagedctl_SOURCES =                                                          \
	storagedctl.c                                                          \
	$(NULL)

storagedctl_CFLAGS =                                                           \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(POLKIT_AGENT_1_CFLAGS)                                               \
	$(NULL)

storagedctl_LDADD =                                                            \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(POLKIT_AGENT_1_LIBS)                                                 \
	$(top_builddir)/storaged/libstoraged.la                                \
	$(NULL)

# ------------------------------------------------------------------------------

sbin_PROGRAMS = umount.storaged

umount_storaged_SOURCES =                                                      \
	umount-storaged.c                                                      \
	$(NULL)

umount_storaged_CFLAGS =                                                       \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(NULL)

umount_storaged_LDADD =                                                        \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(top_builddir)/storaged/libstoraged.la                                \
	$(NULL)

# ------------------------------------------------------------------------------

clean-local:
	rm -f *~
