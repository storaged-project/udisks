#!/usr/bin/python

# very simple example

from gi.repository import UDisks

client = UDisks.Client.new_sync(None)
manager = client.get_object_manager()
objects = manager.get_objects()
for o in objects:
    print '%s:'%o.get_object_path()
    ifaces = o.get_interfaces()
    for i in ifaces:
        print ' IFace %s'%i.get_info().name
    print ''

