[![build status](https://phatina.fedorapeople.org/jenkins/storaged/build.svg)](https://phatina.fedorapeople.org/jenkins/storaged/build.log)


OVERVIEW
========

The Storaged project provides a daemon, tools and libraries to access
and manipulate disks and storage devices.

For API stability and intended audience of Storaged, see the API
STABILITY and AUDIENCE section of the storaged-project(8) man page
(doc/man/storaged-project.xml in the tarball and git repository).


LICENSE
=======

See the COPYING file for the license. In a nutshell, the daemon and
tools are licensed under the GPLv2 (or later) and libraries are
licensed under LGPLv2 (or later).


INSTALLATION
============

Storaged has several dependencies listed in `rpm_dependencies.txt`.

If you run rpm based distro, install the dependencies by:

    # yum install -y $(cat rpm_dependencies.txt)

AUTOTOOLS
---------

To configure and install the Storaged, perform following tasks:

    $ ./autogen.sh

Functionality of storaged is split into several modules:

- iSCSI:

        $ ./configure --enable-iscsi

- LVM2:

        $ ./configure --enable-lvm2

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
