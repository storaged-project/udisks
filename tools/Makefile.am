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

bin_PROGRAMS = udisksctl

udisksctl_SOURCES =                                                            \
	udisksctl.c                                                            \
	$(NULL)

udisksctl_CFLAGS =                                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(POLKIT_AGENT_1_CFLAGS)                                               \
	$(NULL)

udisksctl_LDADD =                                                              \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(POLKIT_AGENT_1_LIBS)                                                 \
	$(top_builddir)/udisks/libudisks2.la                                   \
	$(NULL)

# ------------------------------------------------------------------------------

sbin_PROGRAMS = umount.udisks2

umount_udisks2_SOURCES =                                                       \
	umount-udisks.c                                                        \
	$(NULL)

umount_udisks2_CFLAGS =                                                        \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(NULL)

umount_udisks2_LDADD =                                                         \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(top_builddir)/udisks/libudisks2.la                                   \
	$(NULL)

# ------------------------------------------------------------------------------

clean-local:
	rm -f *~
