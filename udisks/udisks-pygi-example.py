#!/usr/bin/python3
#
# To run this out of the development tree you will need the daemon
# running and you will need to setup a couple environment variables. Assuming
# you are in the root of the source tree do something like:
#
# export GI_TYPELIB_PATH=/usr/lib64/girepository-1.0
# export LD_LIBRARY_PATH=/usr/lib64/udisks2/modules
#

import gi
gi.require_version('UDisks', '2.0')
from gi.repository import UDisks

client = UDisks.Client.new_sync(None)

manager = client.get_manager()
manager.call_enable_modules_sync(True, None)

lvmo = client.get_object("/org/freedesktop/UDisks2/lvm/lvmempty/lvm1")
sdao = client.get_object("/org/freedesktop/UDisks2/block_devices/sda")

#from gi.repository import GLib
#sdao.get_block().call_rescan(GLib.Variant("a{sv}", {}))

#import pdb; pdb.set_trace()

lvmo.get_logical_volume()
#lvmo.get_logical_volume().call_rename_sync('(sa{sv})', "lvm3", {})
#lvmo.get_logical_volume().call_activate_sync()

# call using DBus
#lvm_iface = lvmo.get_interface("org.freedesktop.UDisks2.LogicalVolume")
#lvm_iface.Rename('(sa{sv})', "lvm1", {}, dbus_interface="org.freedesktop.UDisks2.LogicalVolume")

