## Process this file with automake to produce Makefile.in

SUBDIRS = data

ZRAM_MODULE_DIR = $(top_srcdir)/modules/zram

MODULE_SO = libstoraged_zram.so

zramconfdir = $(libdir)/zram.conf.d
zramconf_DATA =

modprobedir = $(libdir)/modprobe.d
modloaddir = $(libdir)/modules-load.d

NULL =

CPPFLAGS =                                                                     \
	-I$(top_builddir) -I$(top_srcdir)                                      \
	-DPACKAGE_LIBEXEC_DIR=\""$(libexecdir)"\"                              \
	-DPACKAGE_SYSCONF_DIR=\""$(sysconfdir)"\"                              \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"                                    \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\"                                      \
	-DPACKAGE_LOCALSTATE_DIR=\""$(localstatedir)"\"                        \
	-DPACKAGE_LOCALE_DIR=\""$(localedir)"\"                                \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\"                                      \
        -DPACKAGE_ZRAMCONF_DIR=\""$(zramconfdir)"\"                            \
        -DPACKAGE_MODPROBE_DIR=\""$(modprobedir)"\"                            \
        -DPACKAGE_MODLOAD_DIR=\""$(modloaddir)"\"                              \
	-DLVM_HELPER_DIR=\""$(prefix)/lib/storaged/"\"                         \
	-D_POSIX_PTHREAD_SEMANTICS -D_REENTRANT                                \
	-DSTORAGED_COMPILATION                                                 \
	$(POLKIT_GOBJECT_1_CFLAGS)                                             \
	$(GLIB_CFLAGS)                                                         \
	$(GIO_CFLAGS)                                                          \
	$(GUDEV_CFLAGS)                                                        \
	$(WARN_CFLAGS)                                                         \
	$(NULL)

$(dbus_built_sources): $(ZRAM_MODULE_DIR)/data/org.storaged.Storaged.zram.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen                                             \
	        --interface-prefix org.storaged.Storaged.                     \
	        --c-namespace Storaged                                        \
	        --generate-c-code storaged-zram-generated                     \
	        --generate-docbook storaged-generated-doc                     \
	        $<

dbus_built_sources =                                                          \
	storaged-zram-generated.h        storaged-zram-generated.c            \
	$(NULL)

BUILT_SOURCES =                                                               \
	$(dbus_built_sources)                                                 \
	$(NULL)

libstoraged_zramdir=$(libdir)/storaged/modules
libstoraged_zram_LTLIBRARIES=libstoraged_zram.la

libstoraged_zramincludedir=$(includedir)/storaged/storaged

libstoraged_zraminclude_HEADERS =                                             \
	$(top_srcdir)/modules/storagedmoduleiface.h                           \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                      \
	$(top_srcdir)/modules/storagedmoduleobject.h                          \
	$(NULL)

libstoraged_zram_la_SOURCES =                                                 \
	$(BUILT_SOURCES)                                                      \
	$(top_srcdir)/modules/storagedmoduleiface.h                           \
	$(top_srcdir)/modules/storagedmoduleifacetypes.h                      \
	$(top_srcdir)/modules/storagedmoduleobject.h                          \
	$(top_srcdir)/modules/storagedmoduleobject.c                          \
	storagedlinuxblockzram.c                                              \
	storagedlinuxblockzram.h                                              \
	storagedlinuxmanagerzram.h                                            \
	storagedlinuxmanagerzram.c                                            \
	storagedzrammoduleiface.c                                             \
	storagedzramstate.h                                                   \
	storagedzramstate.c                                                   \
	storagedzramtypes.h                                                   \
	storagedzramutil.c                                                    \
	storagedzramutil.h                                                    \
	$(NULL)

libstoraged_zram_la_CPPFLAGS =                                                \
	$(CPPFLAGS)                                                           \
	-DG_LOG_DOMAIN=\"libstoraged-zram\"                                   \
	$(NULL)

libstoraged_zram_la_CFLAGS =                                                  \
	$(GLIB_CFLAGS)                                                        \
	$(GIO_CFLAGS)                                                         \
	$(GUDEV_CFLAGS)                                                       \
	$(POLKIT_GOBJECT_1_CFLAGS)                                            \
	$(KBD_CFLAGS)                                                         \
	$(SWAP_CFLAGS)                                                        \
	$(NULL)

libstoraged_zram_la_LDFLAGS =                                                 \
	-export_dynamic                                                       \
	-avoid-version                                                        \
	-module                                                               \
	-no-undefined                                                         \
	-export-symbols-regex                                                 \
	 '^storaged_.*|g_io_module_load|g_io_module_unload|g_io_module_query' \
	$(NULL)

libstoraged_zram_la_LIBADD =                                                  \
	$(GLIB_LIBS)                                                          \
	$(GIO_LIBS)                                                           \
	$(GUDEV_LIBS)                                                         \
	$(POLKIT_GOBJECT_1_LIBS)                                              \
	$(KBD_LIBS)                                                           \
	$(SWAP_LIBS)                                                          \
	$(NULL)

# ------------------------------------------------------------------------------

CLEANFILES = storaged-generated-doc-*.xml storaged-generated.[ch]

EXTRA_DIST =                                                                  \
	$(NULL)

include ../Makefile.uninstalled

all-local: module_link

dist-hook:
	(for i in $(BUILT_SOURCES); do rm -f $(distdir)/$$i; done)

clean-local: module_unlink
	rm -f *~ $(BUILT_SOURCES)
