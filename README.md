OVERVIEW
========

The Storaged project provides a daemon, tools and libraries to access and
manipulate disks, storage devices and technologies.

*Storaged is a fork of UDisks2 and extends its DBus API; it is meant to be a
drop-in replacement for the original project.*

For API stability and intended audience of Storaged, see the API STABILITY and
AUDIENCE section of the `udisks(8)` man page (`doc/man/udisks.xml` in the
tarball and git repository).


LICENSE
=======

See the COPYING file for the license. In a nutshell, the daemon and tools are
licensed under the GPLv2 (or later) and libraries are licensed under LGPLv2 (or
later).


INSTALLATION
============

Storaged has several dependencies listed in `rpm_dependencies.txt`.

If you run rpm based distro, install the dependencies by:

    # dnf install -y $(cat rpm_dependencies.txt)

AUTOTOOLS
---------

To configure and install the Storaged, perform following tasks:

    $ ./autogen.sh

Additional functionality of Storaged for monitoring and management is split
into several modules: *BCache, BTRFS, iSCSI, libStorageManagement, LVM2, LVM
Cache and zRAM*. By default, no additional module will be built.

To build Storaged with (a) chosen module(s), provide or leave these
configuration options for the `configure` script:

    $ ./configure --enable-bcache --enable-btrfs --enable-iscsi
                  --enable-lsm --enable-lvm2 --enable-lvmcache
                  --enable-zram

It is possible to enable all the modules at once:

    $ ./configure --enable-modules

The actual build and installation:

    $ make
    # make install

RELEASES
========

Releases of Storaged are available in compressed tarballs from

 https://github.com/storaged-project/storaged/releases


BUGS and DEVELOPMENT
====================

Please report bugs via the GitHub's issues tracker at

 https://github.com/storaged-project/storaged/issues
