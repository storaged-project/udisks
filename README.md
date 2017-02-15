[![build status](https://phatina.fedorapeople.org/jenkins/storaged/build.svg)](https://phatina.fedorapeople.org/jenkins/storaged/build.log)


OVERVIEW
========

The Udisks project provides a daemon, tools and libraries to access and
manipulate disks, storage devices and technologies.

For API stability and intended audience of Udisks, see the API STABILITY and
AUDIENCE section of the `udisks(8)` man page (`doc/man/udisks.xml` in the
tarball and git repository).


LICENSE
=======

See the COPYING file for the license. In a nutshell, the daemon and tools are
licensed under the GPLv2 (or later) and libraries are licensed under LGPLv2 (or
later).


INSTALLATION
============

Udisks has several dependencies listed in `packaging/udisks2.spec`.

If you run rpm based distro, install the dependencies by:

    # dnf builddep -y packaging/udisks2.spec

AUTOTOOLS
---------

To configure and install the Udisks, perform following tasks:

    $ ./autogen.sh

Additional functionality of Udisks for monitoring and management is split
into several modules: *BCache, BTRFS, iSCSI, libStorageManagement, LVM2, LVM
Cache and zRAM*. By default, no additional module will be built.

To build Udisks with (a) chosen module(s), provide or leave these
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

Releases of Udisks are available in compressed tarballs from

 https://github.com/storaged-project/udisks/releases


BUGS and DEVELOPMENT
====================

Please report bugs via the GitHub's issues tracker at

 https://github.com/storaged-project/udisks/issues
