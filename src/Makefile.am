## Process this file with automake to produce Makefile.in

SUBDIRS = . tests

NULL =

EXTRA_DIST =

AM_CPPFLAGS =                                                                  \
	-I$(top_builddir) -I$(top_srcdir)                                      \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"                              \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"                              \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"                                    \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\"                                      \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\"                        \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\"                                \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\"                                      \
	-DSTORAGED_MODULE_DIR=\"$(libdir)/storaged/modules\"                   \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                                \
	-DSTORAGED_COMPILATION                                                 \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GUDEV_CFLAGS)                                                        \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GMODULE_CFLAGS)                                                      \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

# ------------------------------------------------------------------------------

BUILT_SOURCES =                                                                \
	storaged-daemon-marshal.h    storaged-daemon-marshal.c                 \
	$(NULL)

storaged-daemon-marshal.h: storaged-daemon-marshal.list
	glib-genmarshal $< --prefix=storaged_daemon_marshal --header > $@

storaged-daemon-marshal.c: storaged-daemon-marshal.list
	echo "#include \"storaged-daemon-marshal.h\"" > $@ &&                  \
	        glib-genmarshal $< --prefix=storaged_daemon_marshal --body >> $@

EXTRA_DIST += storaged-daemon-marshal.list

# ------------------------------------------------------------------------------

noinst_LTLIBRARIES = libstoraged-daemon.la

libstoraged_daemon_la_SOURCES =                                                \
	storageddaemontypes.h                                                  \
	storageddaemon.h                 storageddaemon.c                      \
	storagedprovider.h               storagedprovider.c                    \
	storagedlinuxprovider.h          storagedlinuxprovider.c               \
	storagedlinuxblockobject.h       storagedlinuxblockobject.c            \
	storagedlinuxblock.h             storagedlinuxblock.c                  \
	storagedlinuxpartition.h         storagedlinuxpartition.c              \
	storagedlinuxpartitiontable.h    storagedlinuxpartitiontable.c         \
	storagedlinuxfilesystem.h        storagedlinuxfilesystem.c             \
	storagedlinuxencrypted.h         storagedlinuxencrypted.c              \
	storagedlinuxswapspace.h         storagedlinuxswapspace.c              \
	storagedlinuxloop.h              storagedlinuxloop.c                   \
	storagedlinuxdriveobject.h       storagedlinuxdriveobject.c            \
	storagedlinuxdrive.h             storagedlinuxdrive.c                  \
	storagedlinuxdriveata.h          storagedlinuxdriveata.c               \
	storagedlinuxmdraidobject.h      storagedlinuxmdraidobject.c           \
	storagedlinuxmdraid.h            storagedlinuxmdraid.c                 \
	storagedlinuxmanager.h           storagedlinuxmanager.c                \
	storagedlinuxfsinfo.h            storagedlinuxfsinfo.c                 \
	storagedbasejob.h                storagedbasejob.c                     \
	storagedspawnedjob.h             storagedspawnedjob.c                  \
	storagedthreadedjob.h            storagedthreadedjob.c                 \
	storagedsimplejob.h              storagedsimplejob.c                   \
	storagedmount.h                  storagedmount.c                       \
	storagedmountmonitor.h           storagedmountmonitor.c                \
	storageddaemonutil.h             storageddaemonutil.c                  \
	storagedlogging.h                storagedlogging.c                     \
	storagedstate.h                  storagedstate.c                       \
	storagedprivate.h                                                      \
	storagedfstabentry.h             storagedfstabentry.c                  \
	storagedfstabmonitor.h           storagedfstabmonitor.c                \
	storagedcrypttabentry.h          storagedcrypttabentry.c               \
	storagedcrypttabmonitor.h        storagedcrypttabmonitor.c             \
	storagedlinuxdevice.h            storagedlinuxdevice.c                 \
	storagedata.h                    storagedata.c                         \
	storagedmodulemanager.h          storagedmodulemanager.c               \
	$(top_srcdir)/modules/storagedmoduleobject.h                           \
	$(top_srcdir)/modules/storagedmoduleobject.c                           \
	$(BUILT_SOURCES)                                                       \
	$(NULL)

libstoraged_daemon_la_CFLAGS =                                                 \
	-I$(top_srcdir)                                                        \
	-DG_LOG_DOMAIN=\"storaged\"                                            \
	-DBUILDDIR=\"$(abs_top_builddir)\"                                     \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GMODULE_CFLAGS)                                                      \
	$(GUDEV_CFLAGS)                                                        \
	$(LIBATASMART_CFLAGS)                                                  \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(ACL_CFLAGS)                                                          \
	$(LIBSYSTEMD_LOGIN_CFLAGS)                                             \
	$(NULL)

libstoraged_daemon_la_LIBADD =                                                 \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GMODULE_LIBS)                                                        \
	$(GUDEV_LIBS)                                                          \
	$(LIBATASMART_LIBS)                                                    \
	$(POLKIT_GOBJECT_1_LIBS)                                               \
	$(ACL_LIBS)                                                            \
	$(LIBSYSTEMD_LOGIN_LIBS)                                               \
	$(top_builddir)/storaged/libstoraged.la                                \
	$(NULL)

# ------------------------------------------------------------------------------

storageddprivdir = $(prefix)/lib/storaged
storageddpriv_PROGRAMS = storaged

storaged_SOURCES =                                                             \
	main.c                                                                 \
	$(NULL)

storaged_CFLAGS =                                                              \
	-DG_LOG_DOMAIN=\"storaged\"                                            \
	$(NULL)

storaged_LDADD =                                                               \
	$(GLIB_LIBS)                                                           \
	$(GIO_LIBS)                                                            \
	$(GMODULE_LIBS)                                                        \
	libstoraged-daemon.la                                                  \
	$(NULL)

# ------------------------------------------------------------------------------

install-data-local:
	-$(mkdir_p) $(DESTDIR)$(sysconfdir)/storaged
	-$(mkdir_p) $(DESTDIR)$(localstatedir)/lib/storaged
	-chmod 0700 $(DESTDIR)$(localstatedir)/lib/storaged
