#!/usr/bin/python3
#
# very simple example
#
# To run this example out of the development tree you will need the daemon
# running and you will need to setup a couple environment variables.  Assuming
# you are in the root of the source tree do something like:
#
# export GI_TYPELIB_PATH=`pwd`/udisks
# export LD_LIBRARY_PATH=`pwd`/udisks/.libs
#

import gi
gi.require_version('UDisks', '2.0')
from gi.repository import UDisks

client = UDisks.Client.new_sync(None)
manager = client.get_object_manager()
objects = manager.get_objects()
for o in objects:
    print('%s:' % o.get_object_path())
    ifaces = o.get_interfaces()
    for i in ifaces:
        print(' IFace %s' % i.get_interface_name())
    print('')
