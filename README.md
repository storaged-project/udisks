CI status
=========

<img alt="CI status" src="https://fedorapeople.org/groups/storage_apis/statuses/udisks-master.svg" width="100%" height="300ex" />


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
 
 ### Running out of development source tree
 If you would like to run out of the source tree for development without installing,
 please do the following below.  
 
 **Note: Assuming you are in the base of the source tree and
 you don't have udisks already installed**
 
 * Build the source `$ ./autogen.sh --enable-modules --enable-debug && make`
 * To run the daemon and splunk around with dbus clients
   * copy the needed files, policy kit, dbus config, and udev rules
     ```
     sudo cp data/*.policy /usr/share/polkit-1/actions/
     sudo cp modules/*/data/*.policy /usr/share/polkit-1/actions/
     
     sudo cp data/org.freedesktop.UDisks2.conf /etc/dbus-1/system.d/
     
     sudo cp data/80-udisks2.rules /usr/lib/udev/rules.d/
     ```
   * Get the udev rules to run `sudo udevadm control --reload && udevadm trigger && udevadm settle`
 * Start the daemon `# ./udisksd --debug --uninstalled --force-load-modules`
 * Start a client, eg. `# d-feet`
 
 ### Run the unit tests

 `./autogen.sh --enable-modules --enable-debug && make && make ci`
   
 
