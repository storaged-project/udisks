from __future__ import print_function

import unittest
import dbus
import subprocess
import os
import time
import re
import sys
from datetime import datetime
from systemd import journal

if sys.version_info.major == 3 and sys.version_info.minor >= 3:
    from time import monotonic
else:
    from monotonic import monotonic

import gi
gi.require_version('GUdev', '1.0')
from gi.repository import GUdev

test_devs = None
FLIGHT_RECORD_FILE = "flight_record.log"

def run_command(command):
    res = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)

    out, err = res.communicate()
    if res.returncode != 0:
        output = out.decode().strip() + "\n\n" + err.decode().strip()
    else:
        output = out.decode().strip()
    return (res.returncode, output)

def get_call_long(call):
    def call_long(*args, **kwargs):
        """Do an async call with a very long timeout (unless specified otherwise)"""
        kwargs['timeout'] = 100  # seconds
        return call(*args, **kwargs)

    return call_long


def get_version_from_pretty_name(pretty_name):
    """ Try to get distro and version from 'OperatingSystemPrettyName'
        hostname property.

        It should look like this:
         - "Debian GNU/Linux 9 (stretch)"
         - "Fedora 27 (Workstation Edition)"
         - "CentOS Linux 7 (Core)"

        So just return first word as distro and first number as version.
    """
    distro = pretty_name.split()[0].lower()
    match = re.search(r"\d+", pretty_name)
    if match is not None:
        version = match.group(0)
    else:
        raise RuntimeError("Cannot get distro name and version from '%s'" % pretty_name)

    return (distro, version)


def get_version():
    """ Try to get distro and version
    """

    bus = dbus.SystemBus()

    # get information about the distribution from systemd (hostname1)
    sys_info = bus.get_object("org.freedesktop.hostname1", "/org/freedesktop/hostname1")
    cpe = str(sys_info.Get("org.freedesktop.hostname1", "OperatingSystemCPEName", dbus_interface=dbus.PROPERTIES_IFACE))

    if cpe:
        # 2nd to 4th fields from e.g. "cpe:/o:fedoraproject:fedora:25" or "cpe:/o:redhat:enterprise_linux:7.3:GA:server"
        _project, distro, version = tuple(cpe.split(":")[2:5])
    else:
        pretty_name = str(sys_info.Get("org.freedesktop.hostname1", "OperatingSystemPrettyName", dbus_interface=dbus.PROPERTIES_IFACE))
        distro, version = get_version_from_pretty_name(pretty_name)

    # we want just the major version, so remove all decimal places (if any)
    version = str(int(float(version)))

    return (distro, version)


def skip_on(skip_on_distros, skip_on_version="", reason=""):
    """A function returning a decorator to skip some test on a given distribution-version combination

    :param skip_on_distros: distro(s) to skip the test on
    :type skip_on_distros: str or tuple of str
    :param str skip_on_version: version of distro(s) to skip the tests on (only
                                checked on distribution match)

    """
    if isinstance(skip_on_distros, str):
        skip_on_distros = (skip_on_distros,)

    distro, version = get_version()

    def decorator(func):
        if distro in skip_on_distros and (not skip_on_version or skip_on_version == version):
            msg = "not supported on this distribution in this version" + (": %s" % reason if reason else "")
            return unittest.skip(msg)(func)
        else:
            return func

    return decorator

def unstable_test(test):
    """Decorator for unstable tests

    Failures of tests decorated with this decorator are silently ignored unless
    the ``UNSTABLE_TESTS_FATAL`` environment variable is defined.
    """

    def decorated_test(*args):
        try:
            test(*args)
        except unittest.SkipTest:
            # make sure skipped tests are just skipped as usual
            raise
        except:
            # and swallow everything else, just report a failure of an unstable
            # test, unless told otherwise
            if "UNSTABLE_TESTS_FATAL" in os.environ:
                raise
            print("unstable-fail...", end="", file=sys.stderr)

    return decorated_test


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
            try:
                self._update_value()
                if check_fn(self.value):
                    return True
            except Exception:
                # ignore all exceptions -- they might be result of property
                # not having the expected type (e.g. 'None' when checking for len)
                pass
            time.sleep(0.5)

        return False

    def assertEqual(self, value, timeout=TIMEOUT, getter=None):
        if getter is not None:
            check_fn = lambda x: getter(x) == value
        else:
            check_fn = lambda x: x == value
        ret = self._check(timeout, check_fn)

        if not ret:
            if getter is not None:
                raise AssertionError('%s != %s' % (getter(self._value), value))
            else:
                raise AssertionError('%s != %s' % (self._value, value))

    def assertAlmostEqual(self, value, delta, timeout=TIMEOUT, getter=None):
        if getter is not None:
            check_fn = lambda x: abs(getter(x) - value) <= delta
        else:
            check_fn = lambda x: abs(x - value) <= delta
        ret = self._check(timeout, check_fn)

        if not ret:
            if getter is not None:
                raise AssertionError('%s is not almost equal to %s (delta = %s)' % (getter(self._value),
                                                                                    value, delta))
            else:
                raise AssertionError('%s is not almost equal to %s (delta = %s)' % (self._value,
                                                                                    value, delta))


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


class UdisksTestCase(unittest.TestCase):
    iface_prefix = None
    path_prefix = None
    bus = None
    vdevs = None
    distro = (None, None, None)       # (project, distro_name, version)
    no_options = dbus.Dictionary(signature="sv")


    @classmethod
    def setUpClass(self):
        self.iface_prefix = 'org.freedesktop.UDisks2'
        self.path_prefix = '/org/freedesktop/UDisks2'
        self.bus = dbus.SystemBus()

        self.distro = get_version()

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


    def run(self, *args):
        record = []
        now = datetime.now()
        now_mono = monotonic()
        with open(FLIGHT_RECORD_FILE, "a") as record_f:
            record_f.write("================%s[%0.8f] %s.%s.%s================\n" % (now.strftime('%Y-%m-%d %H:%M:%S'),
                                                                                     now_mono,
                                                                                     self.__class__.__module__,
                                                                                     self.__class__.__name__,
                                                                                     self._testMethodName))
            with JournalRecorder("journal", record):
                with CmdFlightRecorder("udisksctl monitor", ["udisksctl", "monitor"], record):
                    with CmdFlightRecorder("udevadm monitor", ["udevadm", "monitor"], record):
                        super(UdisksTestCase, self).run(*args)
            record_f.write("".join(record))
        self.udev_settle()

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
        (appended to the common UDisks2 prefix) or given as the object
        itself.

        :param obj: object to get the interface for
        :type obj: str or dbus.proxies.ProxyObject
        :param iface_suffix: suffix appended to the common UDisks2 interface prefix
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
        except OSError as e:
            if not ignore_nonexistent:
                raise e

    @classmethod
    def remove_file(self, filename, ignore_nonexistent=False):
        try:
            os.remove(filename)
        except OSError as e:
            if not ignore_nonexistent:
                raise e

    @classmethod
    def run_command(self, command):
        return run_command(command)

    @classmethod
    def check_module_loaded(self, module):
        manager_obj = self.get_object('/Manager')
        manager = self.get_interface(manager_obj, '.Manager')
        manager_intro = dbus.Interface(manager_obj, "org.freedesktop.DBus.Introspectable")
        intro_data = manager_intro.Introspect()
        modules_loaded = 'interface name="org.freedesktop.UDisks2.Manager.%s"' % module in intro_data

        if not modules_loaded:
            manager.EnableModules(dbus.Boolean(True))
            intro_data = manager_intro.Introspect()
            return 'interface name="org.freedesktop.UDisks2.Manager.%s"' % module in intro_data
        else:
            return True


    @classmethod
    def ay_to_str(self, ay):
        """Convert a bytearray (terminated with '\0') to a string"""

        return ''.join(chr(x) for x in ay[:-1])

    @classmethod
    def str_to_ay(self, string, terminate=True):
        """Convert a string to a bytearray (terminated with '\0')"""

        if terminate:
            string += '\0'

        return dbus.Array([dbus.Byte(ord(c)) for c in string],
                          signature=dbus.Signature('y'), variant_level=1)

    @classmethod
    def set_udev_property(self, device, prop, value):
        udev = GUdev.Client()
        dev = udev.query_by_device_file(device)
        serial = dev.get_property("ID_SERIAL")

        try:
            os.makedirs("/run/udev/rules.d/")
        except OSError:
            # already exists
            pass

        self.write_file("/run/udev/rules.d/99-udisks_test.rules",
                        'ENV{ID_SERIAL}=="%s", ENV{%s}="%s"\n' % (serial, prop, value))
        self.run_command("udevadm control --reload")
        uevent_path = os.path.join(dev.get_sysfs_path(), "uevent")
        self.write_file(uevent_path, "change\n")
        self.udev_settle()
        os.unlink("/run/udev/rules.d/99-udisks_test.rules")
        self.run_command("udevadm control --reload")

    @classmethod
    def assertHasIface(self, obj, iface):
        obj_intro = dbus.Interface(obj, "org.freedesktop.DBus.Introspectable")
        intro_data = obj_intro.Introspect()

        for _ in range(20):
            if ('interface name="%s"' % iface) in intro_data:
                return
            time.sleep(0.5)

        raise AssertionError("Object '%s' has no interface '%s'" % (obj.object_path, iface))

    def assertStartswith(self, val, prefix):
        if not val.startswith(prefix):
            raise AssertionError("'%s' does not start with '%s'" % (val, prefix))


class FlightRecorder(object):
    """Context manager for recording data/logs

    This is the abstract implementation that does nothing. Subclasses are
    expected to override the methods below to actually do something useful.

    """

    def __init__(self, desc):
        """
        :param str desc: description of the recorder

        """
        self._desc = desc

    def _start(self):
        """Start recording"""

    def _stop(self):
        """Stop recording"""
        pass

    def _save(self):
        """Save the record"""
        pass

    def __enter__(self):
        self._start()

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._stop()
        self._save()

        # Returning False means that the exception we have potentially been
        # given as arguments was not handled
        return False

class CmdFlightRecorder(FlightRecorder):
    """Flight recorder running a command and gathering its standard and error output"""

    def __init__(self, desc, argv, store):
        """
        :param str desc: description of the recorder
        :param argv: command and arguments to run
        :type argv: list of str
        :param store: a list-like object to append the data/logs to

        """
        super(CmdFlightRecorder, self).__init__(desc)
        self._argv = argv
        self._store = store
        self._proc = None

    def _start(self):
        self._proc = subprocess.Popen(self._argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def _stop(self):
        self._proc.terminate()

    def _save(self):
        # err is in out (see above)
        out, _err = self._proc.communicate()
        rec = '<<<<< ' + self._desc + ' >>>>>' + '\n' + out.decode() + '\n\n'
        self._store.append(rec)

class JournalRecorder(FlightRecorder):
    """Flight recorder for gathering logs (journal)"""

    def __init__(self, desc, store):
        """
        :param str desc: description of the recorder
        :param store: a list-like object to append the data/logs to

        """
        super(JournalRecorder, self).__init__(desc)
        self._store = store
        self._started = None
        self._stopped = None

    def _start(self):
        self._started = monotonic()

    def _stop(self):
        self._stopped = monotonic()

    def _save(self):
        j = journal.Reader()
        j.this_boot()
        j.seek_monotonic(self._started)
        journal_data = ""

        entry = j.get_next()
        # entry["__MONOTONIC_TIMESTAMP"] is a tuple of (datetime.timedelta, boot_id)
        while entry and entry["__MONOTONIC_TIMESTAMP"][0].seconds <= int(self._stopped):
            if "_COMM" in entry and "_PID" in entry:
                source = "%s[%d]" % (entry["_COMM"], entry["_PID"])
            else:
                source = "kernel"
            journal_data += "%s[%0.8f] %s: %s\n" % (entry["__REALTIME_TIMESTAMP"].strftime("%H:%M:%S"),
                                                    entry["__MONOTONIC_TIMESTAMP"][0].total_seconds(),
                                                    source, entry["MESSAGE"])
            entry = j.get_next()
        rec = '<<<<< ' + self._desc + ' >>>>>' + '\n' + journal_data + '\n\n\n'
        self._store.append(rec)
