%global glib2_version                   2.36
%global gobject_introspection_version   1.30.0
%global polkit_version                  0.102
%global systemd_version                 208
%global libatasmart_version             0.17
%global dbus_version                    1.4.0
%global with_gtk_doc                    1

%define is_fedora                       0%{?rhel} == 0

Name:    storaged
Summary: Disk Manager
Version: 2.4.0
Release: 5%{?dist}
License: GPLv2+
Group:   System Environment/Libraries
URL:     https://github.com/storaged-project/storaged
Source0: https://github.com/storaged-project/storaged/releases/download/%{name}-%{version}/%{name}-%{version}.tar.bz2

BuildRequires: glib2-devel >= %{glib2_version}
BuildRequires: gobject-introspection-devel >= %{gobject_introspection_version}
BuildRequires: libgudev1-devel >= %{systemd_version}
BuildRequires: libatasmart-devel >= %{libatasmart_version}
BuildRequires: polkit-devel >= %{polkit_version}
BuildRequires: systemd-devel >= %{systemd_version}
BuildRequires: gnome-common
BuildRequires: libacl-devel
BuildRequires: chrpath
BuildRequires: gtk-doc
BuildRequires: intltool

# Needed to pull in the system bus daemon
Requires: dbus >= %{dbus_version}
# Needed to pull in the udev daemon
Requires: systemd >= %{systemd_version}
# We need at least this version for bugfixes/features etc.
Requires: libatasmart >= %{libatasmart_version}
# For mount, umount, mkswap
Requires: util-linux
# For mkfs.ext3, mkfs.ext3, e2label
Requires: e2fsprogs
# For mkfs.xfs, xfs_admin
Requires: xfsprogs
# For mkfs.vfat
Requires: dosfstools
# For partitioning
Requires: parted
Requires: gdisk
# For LUKS devices
Requires: cryptsetup-luks
# For ejecting removable disks
Requires: eject
# For MD-RAID
Requires: mdadm

Requires: libstoraged%{?_isa} = %{version}-%{release}

# For mkntfs (not available on rhel or on ppc/ppc64)
%if ! 0%{?rhel}
%ifnarch ppc ppc64
Requires: ntfsprogs
%endif
%endif

# For /proc/self/mountinfo, only available in 2.6.26 or higher
Conflicts: kernel < 2.6.26

%description
The Storaged project provides a daemon, tools and libraries to access and
manipulate disks, storage devices and technologies.

%package -n libstoraged
Summary: Dynamic library to access the storaged daemon
Group: System Environment/Libraries
License: LGPLv2+

%description -n libstoraged
This package contains the dynamic library storaged, which provides
access to the storaged daemon.

%package -n storaged-iscsi
Summary: Module for iSCSI
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
License: LGPLv2+
Requires: iscsi-initiator-utils
BuildRequires: iscsi-initiator-utils-devel

%description -n storaged-iscsi
This package contains module for iSCSI configuration.

%package -n storaged-lvm2
Summary: Module for LVM2
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
License: LGPLv2+
Requires: lvm2
BuildRequires: lvm2-devel

%description -n storaged-lvm2
This package contains module for LVM2 configuration.

%package -n libstoraged-devel
Summary: Development files for libstoraged
Group: Development/Libraries
Requires: libstoraged%{?_isa} = %{version}-%{release}
License: LGPLv2+

%description -n libstoraged-devel
This package contains the development files for the library
libstoraged, a dynamic library, which provides access to the storaged
daemon.

%if %{is_fedora}
%package -n storaged-bcache
Summary: Module for Bcache
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
License: LGPLv2+
Requires: libblockdev-kbd
BuildRequires: libblockdev-kbd-devel

%description -n storaged-bcache
This package contains module for Bcache configuration.

%package -n storaged-btrfs
Summary: Module for BTRFS
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
License: LGPLv2+
Requires: libblockdev-btrfs
BuildRequires: libblockdev-btrfs-devel

%description -n storaged-btrfs
This package contains module for BTRFS configuration.

%package -n storaged-lsm
Summary: Module for LSM
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
License: LGPLv2+
Requires: libstoragemgmt
BuildRequires: libstoragemgmt-devel
BuildRequires: libconfig-devel

%description -n storaged-lsm
This package contains module for LSM configuration.

%package -n storaged-zram
Summary: Module for ZRAM
Group: System Environment/Libraries
Requires: %{name}%{?_isa} = %{version}-%{release}
License: LGPLv2+
Requires: libblockdev-kbd
Requires: libblockdev-swap
BuildRequires: libblockdev-kbd-devel
BuildRequires: libblockdev-swap-devel

%description -n storaged-zram
This package contains module for ZRAM configuration.
%endif

%prep
%setup -q -n storaged-%{version}

%build
autoreconf -ivf
%configure            \
    --sysconfdir=/etc \
%if %{with_gtk_doc}
    --enable-gtk-doc  \
%else
    --disable-gtk-doc \
%endif
%if %{is_fedora}
    --enable-modules
%else
    --enable-iscsi    \
    --enable-lvm2
%endif
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}
%if %{with_gtk_doc} == 0
rm -fr %{buildroot}/%{_datadir}/gtk-doc/html/storaged
%endif

find %{buildroot} -name \*.la -o -name \*.a | xargs rm

chrpath --delete %{buildroot}/%{_sbindir}/umount.storaged
chrpath --delete %{buildroot}/%{_bindir}/storagectl
chrpath --delete %{buildroot}/%{_libexecdir}/storaged/storaged

%find_lang %{name}

%post -n storaged
udevadm control --reload
udevadm trigger

%post -n libstoraged -p /sbin/ldconfig

%postun -n libstoraged -p /sbin/ldconfig

%files -f %{name}.lang
%doc README.md AUTHORS NEWS HACKING
%license COPYING

%dir %{_sysconfdir}/storaged
%if %{is_fedora}
%dir %{_sysconfdir}/storaged/modules.conf.d
%endif
%{_sysconfdir}/storaged/storaged.conf

%{_sysconfdir}/dbus-1/system.d/org.storaged.Storaged.conf
%{_datadir}/bash-completion/completions/storagectl
%{_prefix}/lib/systemd/system/storaged.service
%{_prefix}/lib/systemd/system/zram-setup@.service
%{_prefix}/lib/udev/rules.d/80-storaged.rules
%{_sbindir}/umount.storaged

%dir %{_prefix}/lib/storaged
%{_libexecdir}/storaged/storaged

%{_bindir}/storagectl

%{_mandir}/man1/storagectl.1*
%{_mandir}/man5/storaged.conf.5*
%{_mandir}/man8/storaged.8*
%{_mandir}/man8/storaged-project.8*
%{_mandir}/man8/umount.storaged.8*

%{_datadir}/polkit-1/actions/org.storaged.Storaged.policy
%{_datadir}/dbus-1/system-services/org.storaged.Storaged.service

# Permissions for local state data are 0700 to avoid leaking information
# about e.g. mounts to unprivileged users
%attr(0700,root,root) %dir %{_localstatedir}/lib/storaged

%files -n libstoraged
%{_libdir}/libstoraged.so.*
%{_libdir}/girepository-1.0/Storaged-2.0.typelib

%files -n storaged-lvm2
%dir %{_prefix}/lib/storaged
%dir %{_libdir}/storaged
%dir %{_libdir}/storaged/lvm-nolocking
%dir %{_libdir}/storaged/modules
%{_prefix}/lib/storaged/storaged-lvm
%{_libdir}/storaged/lvm-nolocking/lvm.conf
%{_libdir}/storaged/modules/libstoraged_lvm2.so
%{_datadir}/polkit-1/actions/org.storaged.Storaged.lvm2.policy
%{_mandir}/man8/storaged-lvm.8*

%files -n storaged-iscsi
%dir %{_libdir}/storaged
%dir %{_libdir}/storaged/modules
%{_libdir}/storaged/modules/libstoraged_iscsi.so
%{_datadir}/polkit-1/actions/org.storaged.Storaged.iscsi.policy

%files -n libstoraged-devel
%{_libdir}/libstoraged.so
%dir %{_includedir}/storaged
%dir %{_includedir}/storaged/storaged
%{_includedir}/storaged/storaged/*.h
%{_datadir}/gir-1.0/Storaged-2.0.gir
%if %{with_gtk_doc}
%dir %{_datadir}/gtk-doc/html/storaged
%{_datadir}/gtk-doc/html/storaged/*
%endif
%{_libdir}/pkgconfig/storaged.pc

%if %{is_fedora}
%files -n storaged-bcache
%dir %{_libdir}/storaged
%dir %{_libdir}/storaged/modules
%{_libdir}/storaged/modules/libstoraged_bcache.so
%{_datadir}/polkit-1/actions/org.storaged.Storaged.bcache.policy

%files -n storaged-btrfs
%dir %{_libdir}/storaged
%dir %{_libdir}/storaged/modules
%{_libdir}/storaged/modules/libstoraged_btrfs.so
%{_datadir}/polkit-1/actions/org.storaged.Storaged.btrfs.policy

%files -n storaged-lsm
%dir %{_libdir}/storaged
%dir %{_libdir}/storaged/modules
%dir %{_sysconfdir}/storaged/modules.conf.d
%{_libdir}/storaged/modules/libstoraged_lsm.so
%{_mandir}/man5/storaged_lsm.conf.*
%{_sysconfdir}/storaged/modules.conf.d/storaged_lsm.conf

%files -n storaged-zram
%dir %{_libdir}/storaged
%dir %{_libdir}/storaged/modules
%{_libdir}/storaged/modules/libstoraged_zram.so
%{_datadir}/polkit-1/actions/org.storaged.Storaged.zram.policy
%endif

%changelog
* Wed Feb 10 2016 Peter Hatina <phatina@redhat.com> - 2.4.0-5
- Package template zram-setup@.service file

* Wed Feb 10 2016 Peter Hatina <phatina@redhat.com> - 2.4.0-4
- Add storaged configuration file and its man page

* Thu Jan 21 2016 Peter Hatina <phatina@redhat.com> - 2.4.0-3
- Redesign subpackage dependencies
- Make GTK documentation generation configurable

* Wed Jan 20 2016 Peter Hatina <phatina@redhat.com> - 2.4.0-2
- Reload udev rules and trigger events when installed

* Wed Jan 13 2016 Peter Hatina <phatina@redhat.com> - 2.4.0-1
- Upgrade to 2.4.0

* Wed Sep 30 2015 Peter Hatina <phatina@redhat.com> - 2.3.0-2
- Add Fedora/RHEL package configuration options

* Mon Sep 14 2015 Peter Hatina <phatina@redhat.com> - 2.3.0-1
- Change BuildRequires from pkgconfig macro to -devel packages
- Upgrade to 2.3.0

* Mon Aug 24 2015 Peter Hatina <phatina@redhat.com> - 2.2.0-1
- Upgrade to 2.2.0

* Fri Jul  3 2015 Peter Hatina <phatina@redhat.com> - 2.1.1-1
- Upgrade to 2.1.1

* Wed Jun 24 2015 Peter Hatina <phatina@redhat.com> - 2.1.0-4
- Add Requires for storaged modules

* Wed Jun 24 2015 Peter Hatina <phatina@redhat.com> - 2.1.0-3
- Changes for EPEL-7
  - Lower systemd required version to 208
  - Rewrite BuildRequires for systemd-devel

* Fri Jun 19 2015 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 2.1.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_23_Mass_Rebuild

* Thu Jun 11 2015 Peter Hatina <phatina@redhat.com> - 2.1.0-1
- Update to upstream 2.1.0

* Thu Apr 02 2015 Peter Hatina <phatina@redhat.com> - 2.0.0-1
- Rebase to the new Storaged implementation
- Upstream: https://storaged.org

* Tue Sep 16 2014 Stef Walter <stefw@redhat.com> - 0.3.1-1
- Update to upstream 0.3.1

* Mon Aug 18 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.3.0-3
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_22_Mass_Rebuild

* Sun Jun 08 2014 Fedora Release Engineering <rel-eng@lists.fedoraproject.org> - 0.3.0-2
- Rebuilt for https://fedoraproject.org/wiki/Fedora_21_Mass_Rebuild

* Tue Apr 08 2014 Patrick Uiterwijk <puiterwijk@redhat.com> - 0.3.0-1
- Update to upstream 0.3.0

* Fri Jan 31 2014 Patrick Uiterwijk <puiterwijk@redhat.com> - 0.2.0-1
- Update to upstream 0.2.0

* Thu Jan 16 2014 Patrick Uiterwijk <puiterwijk@redhat.com> - 0.1.0-2
- Removed double systemd BuildRequire
- Rewritten summary and description

* Sun Jan 12 2014 Patrick Uiterwijk <puiterwijk@redhat.com> - 0.1.0-1
- Rename from udisks2-lvm
