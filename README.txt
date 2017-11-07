TODO: This file should not be part of the final commit
* work in progress, sorry about the mess

Introspection work so far:

* added new directory "generated"
  ** contains new Makefile.am that runs before everything else
     to generate all generated files (udisks + modules)
  ** works, no attention needed

* added new directory "gi" <- INTROSPECTION LOCATED HERE
  ** contains new Makefile.am that runs after everything else
  ** introspection has to be run last when everything is done (it runs its own compilation)
  ** contents mostly moved from/based on ./udisks/Makefile.am
  ** Makefile called correctly, introspection runs ok on core functions

* how to compile and run (on VM):
  ** sudo git clean -xdf (purge)
  ** ./autogen.sh --enable-modules --disable-gtk-doc --prefix=/usr && make
     *** make produces bunch of warnings that I think may be related to the issue
  ** sudo make install

* how to reproduce the issue:
  ** as root:
  ** # export LD_LIBRARY_PATH=/usr/lib64/udisks2/modules
  ** # ./udisks/udisks-pygi-example.py
  ** produces two debug udisks warnings (source: ./modules/lvm2/udiskslvm2dbusutil.c:154,165)
  ** produces "./udisks/udisks-pygi-example.py:28: Warning: invalid cast from 'GDBusProxy' to 'UDisksLogicalVolume'"
     which is the main problem
  ** warning itself is correctly raised in the "UDISKS_LOGICAL_VOLUME" macro

Introspection information:
man g-ir-scanner
man g-ir-compiler
https://wiki.gnome.org/Projects/GObjectIntrospection/AutotoolsIntegration
