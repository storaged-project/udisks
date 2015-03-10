OVERVIEW
========

The storaged project provides a daemon, tools and libraries to access
and manipulate disks and storage devices.

For API stability and intended audience of storaged, see the API
STABILITY and AUDIENCE section of the storaged(8) man page
(doc/man/storaged.xml in the tarball and git repository).


LICENSE
=======

See the COPYING file for the license. In a nutshell, the daemon and
tools are licensed under the GPLv2 (or later) and libraries are
licensed under LGPLv2 (or later).


INSTALLATION
============

Storaged has several dependencies listed in `yum_dependencies.txt`.

If you run yum based distro, install the dependencies by:

    # yum install -y $(cat yum_dependencies)

AUTOTOOLS
---------

To configure and install the Storaged, perform following tasks:

    $ ./autogen.sh

Optional module for LVM support:

    $ ./configure --enable-lvm2

The actual build and installation:

    $ make
    # make install

RELEASES
========

Releases of storaged are available in compressed tarballs from

 https://github.com/phatina/storaged/releases


BUGS and DEVELOPMENT
====================

Please report bugs via the GitHub's issues tracker at

 https://github.com/phatina/storaged/issues
