import unittest
import dbus
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


class DBusProperty(object):

    TIMEOUT = 5

    def __init__(self, obj, iface, prop):
        self.obj = obj
        self.iface = iface
        self.prop = prop

        self._value = None

    @property
    def value(self):
        if self._value is None:
            self._update_value()
        return self._value

    def _update_value(self):
        self._value = self.obj.Get(self.iface, self.prop, dbus_interface=dbus.PROPERTIES_IFACE)

    def _check(self, timeout, check_fn):
        for _ in range(int(timeout / 0.5)):
            self._update_value()
            try:
                if check_fn(self.value):
                    return True
            except Exception:
                # ignore all exceptions -- they might be result of property
                # not having the expected type (e.g. 'None' when checking for len)
                pass
            time.sleep(0.5)

        return False

    def assertEqual(self, value, timeout=TIMEOUT):
        check_fn = lambda x: x == value
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('%s != %s' % (self._value, value))

    def assertGreater(self, value, timeout=TIMEOUT):
        check_fn = lambda x: x > value
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('%s is not grater than %s' % (self._value, value))

    def assertLess(self, value, timeout=TIMEOUT):
        check_fn = lambda x: x < value
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('%s is not less than %s' % (self._value, value))

    def assertIn(self, lst, timeout=TIMEOUT):
        check_fn = lambda x: x in lst
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('%s not found in %s' % (self._value, lst))

    def assertNotIn(self, lst, timeout=TIMEOUT):
        check_fn = lambda x: x not in lst
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('%s unexpectedly found in %s' % (self._value, lst))

    def assertTrue(self, timeout=TIMEOUT):
        check_fn = lambda x: bool(x)
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('%s is not true' % self._value)

    def assertFalse(self, timeout=TIMEOUT):
        check_fn = lambda x: not bool(x)
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('%s is not false' % self._value)

    def assertIsNone(self, timeout=TIMEOUT):
        check_fn = lambda x: x is None
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('%s is not None' % self._value)

    def assertIsNotNone(self, timeout=TIMEOUT):
        check_fn = lambda x: x is not None
        ret = self._check(timeout, check_fn)

        if not ret:
            raise AssertionError('unexpectedly None')

    def assertLen(self, length, timeout=TIMEOUT):
        check_fn = lambda x: len(x) == length
        ret = self._check(timeout, check_fn)

        if not ret:
            if not hasattr(self._value, '__len__'):
                raise AssertionError('%s has no length' % type(self._value))
            else:
                raise AssertionError('Expected length %d, but %s has length %d' % (length,
                                                                                   self._value,
                                                                                   len(self._value)))


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
    def get_object(self, path_suffix):
        # if given full path, just use it, otherwise prepend the prefix
        if path_suffix.startswith(self.path_prefix):
            path = path_suffix
        else:
            path = self.path_prefix + path_suffix
        try:
            # self.iface_prefix is the same as the DBus name we acquire
            obj = self.bus.get_object(self.iface_prefix, path)
        except:
            obj = None
        return obj

    @classmethod
    def get_interface(self, obj, iface_suffix):
        """Get interface for the given object either specified by an object path suffix
        (appended to the common UDisks2/storaged prefix) or given as the object
        itself.

        :param obj: object to get the interface for
        :type obj: str or dbus.proxies.ProxyObject
        :param iface_suffix: suffix appended to the common UDisks2/storaged interface prefix
        :type iface_suffix: str

        """
        if isinstance(obj, str):
            obj = self.get_object(obj)
        return dbus.Interface(obj, self.iface_prefix + iface_suffix)


    @classmethod
    def get_property(self, obj, iface_suffix, prop):
        return DBusProperty(obj, self.iface_prefix + iface_suffix, prop)


    @classmethod
    def get_property_raw(self, obj, iface_suffix, prop):
        res = obj.Get(self.iface_prefix + iface_suffix, prop, dbus_interface=dbus.PROPERTIES_IFACE)
        return res

    @classmethod
    def get_device(self, dev_name):
        """Get block device object for a given device (e.g. "sda")"""
        dev = self.get_object('/block_devices/' + os.path.basename(dev_name))
        return dev

    @classmethod
    def get_drive_name(self, device):
        """Get drive name for the given block device object"""
        drive_name = self.get_property_raw(device, '.Block', 'Drive').split('/')[-1]
        return drive_name

    @classmethod
    def udev_settle(self):
        self.run_command('udevadm settle')

    @classmethod
    def wipe_fs(self, device):
        for _ in range(10):
            ret, _out = self.run_command('wipefs -a %s' % device)
            if ret == 0:
                return True
            time.sleep(1)

        return False

    @classmethod
    def read_file(self, filename):
        with open(filename, 'r') as f:
            content = f.read()
        return content


    @classmethod
    def write_file(self, filename, content, ignore_nonexistent=False):
        try:
            with open(filename, 'w') as f:
                f.write(content)
        except FileNotFoundError as e:
            if not ignore_nonexistent:
                raise e

    @classmethod
    def run_command(self, command):
        res = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)

        out, _err = res.communicate()
        return (res.returncode, out.decode().strip())

    @classmethod
    def ensure_modules_loaded(self):
        manager_obj = self.get_object('/Manager')
        manager = self.get_interface(manager_obj, '.Manager')
        manager_intro = dbus.Interface(manager_obj, "org.freedesktop.DBus.Introspectable")
        intro_data = manager_intro.Introspect()
        modules_loaded = 'interface name="org.freedesktop.UDisks2.Manager.Bcache"' in intro_data

        if not modules_loaded:
            manager.EnableModules(dbus.Boolean(True))
            intro_data = manager_intro.Introspect()
            assert 'interface name="org.freedesktop.UDisks2.Manager.Bcache"' in intro_data


    @classmethod
    def ay_to_str(self, ay):
        """Convert a bytearray (terminated with '\0') to a string"""

        return ''.join(chr(x) for x in ay[:-1])

    @classmethod
    def str_to_ay(self, string):
        """Convert a string to a bytearray (terminated with '\0')"""

        string += '\0'

        return dbus.Array([dbus.Byte(ord(c)) for c in string],
                          signature=dbus.Signature('y'), variant_level=1)
