import unittest
import dbus
import sys
import subprocess
import os
import time

daemon_bin = None
test_devs = None

def get_call_long(call):
    def call_long(*args, **kwargs):
        """Do an async call with a very long timeout (unless specified otherwise)"""
        kwargs['timeout'] = 100  # seconds
        return call(*args, **kwargs)

    return call_long

class StoragedTestCase(unittest.TestCase):
    iface_prefix = None
    path_prefix = None
    bus = None
    vdevs = None
    no_options = dbus.Dictionary(signature="sv")


    @classmethod
    def setUpClass(self):
        if daemon_bin == 'udisksd':
            self.iface_prefix = 'org.freedesktop.UDisks2'
            self.path_prefix = '/org/freedesktop/UDisks2'
        elif daemon_bin == 'storaged':
            self.iface_prefix = 'org.storaged.Storaged'
            self.path_prefix = '/org/storaged/Storaged'
        self.bus = dbus.SystemBus()
        self._orig_call_async = self.bus.call_async
        self._orig_call_blocking = self.bus.call_blocking
        self.bus.call_async = get_call_long(self._orig_call_async)
        self.bus.call_blocking = get_call_long(self._orig_call_blocking)
        self.vdevs = test_devs
        assert len(self.vdevs) > 3;


    @classmethod
    def tearDownClass(self):
        self.bus.call_async = self._orig_call_async
        self.bus.call_blocking = self._orig_call_blocking


    @classmethod
    def get_object(self, iface_suffix, path_suffix):
        try:
            obj = self.bus.get_object(self.iface_prefix + iface_suffix, self.path_prefix + path_suffix)
        except:
            obj = None
        return obj


    @classmethod
    def get_property(self, obj, iface_suffix, prop):
        try:
            res = obj.Get(self.iface_prefix + iface_suffix, prop, dbus_interface=dbus.PROPERTIES_IFACE)
        except:
            res = None
        return res


    @classmethod
    def udev_settle(self):
        self.run_command('udevadm settle')


    @classmethod
    def read_file(self, filename):
        with open(filename, 'r') as f:
            content = f.read()
        return content


    @classmethod
    def write_file(self, filename, content):
        with open(filename, 'w') as f:
            f.seek(0)
            f.write(content)
            f.truncate()


    @classmethod
    def run_command(self, command):
        res = subprocess.run(command, shell=True, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        out = res.stdout.decode().strip()
        return (res.returncode, out)
