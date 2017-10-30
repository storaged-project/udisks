import dbus
import six
import shutil
import re
import tempfile
import unittest

from bytesize import bytesize
from collections import namedtuple
from contextlib import contextmanager
from distutils.version import LooseVersion

import udiskstestcase
from udiskstestcase import unstable_test


Device = namedtuple('Device', ['obj', 'obj_path', 'path', 'name', 'size'])


class UdisksBtrfsTest(udiskstestcase.UdisksTestCase):
    '''This is a basic test suite for btrfs interface'''

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('BTRFS'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for btrfs tests not loaded, skipping.')

    @contextmanager
    def _temp_mount(self, device):
        tmp = tempfile.mkdtemp()
        self.run_command('mount %s %s' % (device, tmp))

        try:
            yield tmp
        finally:
            self.run_command('umount %s' % tmp)
            shutil.rmtree(tmp)

    def _clean_format(self, device):
        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        device.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

    def _get_devices(self, num_devices):
        devices = []

        for i in range(num_devices):
            dev_path = self.vdevs[i]
            dev_name = dev_path.split('/')[-1]

            dev_obj = self.get_object('/block_devices/' + dev_name)
            self.assertIsNotNone(dev_obj)

            _ret, out = self.run_command('lsblk -d -b -no SIZE %s' % dev_path)  # get size of the device

            dev = Device(dev_obj, dbus.ObjectPath(dev_obj.object_path), dev_path, dev_name, int(out))
            devices.append(dev)

        return devices

    def _get_btrfs_version(self):
        _ret, out = self.run_command('btrfs --version')
        m = re.search(r'[Bb]trfs.* v([\d\.]+)', out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError('Failed to determine btrfs version from: %s' % out)
        return LooseVersion(m.groups()[0])

    def test_create(self):
        dev = self._get_devices(1)[0]
        self.addCleanup(self._clean_format, dev.obj)

        manager = self.get_object('/Manager')
        manager.CreateVolume([dev.obj_path],
                             'test_single', 'single', 'single',
                             self.no_options,
                             dbus_interface=self.iface_prefix + '.Manager.BTRFS')

        self.write_file("/sys/block/%s/uevent" % dev.name, "change\n")

        # check filesystem type
        usage = self.get_property(dev.obj, '.Block', 'IdUsage')
        usage.assertEqual('filesystem')

        fstype = self.get_property(dev.obj, '.Block', 'IdType')
        fstype.assertEqual('btrfs')

        # check '.Filesystem.BTRFS' properties
        dbus_label = self.get_property(dev.obj, '.Filesystem.BTRFS', 'label')
        _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % dev.path)
        dbus_label.assertEqual('test_single')
        dbus_label.assertEqual(sys_label)

        dbus_uuid = self.get_property(dev.obj, '.Filesystem.BTRFS', 'uuid')
        _ret, sys_uuid = self.run_command('lsblk -d -no UUID %s' % dev.path)
        dbus_uuid.assertEqual(sys_uuid)

        dbus_devs = self.get_property(dev.obj, '.Filesystem.BTRFS', 'num_devices')
        dbus_devs.assertEqual(1)

        # check size
        with self._temp_mount(dev.path) as mnt:
            _ret, out = self.run_command('btrfs filesystem show %s' % mnt)
            m = re.search(r'.*devid +1 size (.+) used', out, flags=re.DOTALL)
            sys_size = bytesize.Size(m.group(1))
            self.assertEqual(sys_size.convert_to(bytesize.B), dev.size)

    @unstable_test
    def test_create_raid(self):
        devs = self._get_devices(2)

        for dev in devs:
            self.addCleanup(self._clean_format, dev.obj)

        manager = self.get_object('/Manager')

        # invalid raid level
        msg = '[uU]nknown profile raidN'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            manager.CreateVolume([dev.obj_path for dev in devs],
                                 'test_raidN', 'raidN', 'raidN',
                                 self.no_options,
                                 dbus_interface=self.iface_prefix + '.Manager.BTRFS')


        manager.CreateVolume([dev.obj_path for dev in devs],
                             'test_raid1', 'raid1', 'raid1',
                             self.no_options,
                             dbus_interface=self.iface_prefix + '.Manager.BTRFS')

        # check filesystem type
        for dev in devs:
            usage = self.get_property(dev.obj, '.Block', 'IdUsage')
            usage.assertEqual('filesystem')

            fstype = self.get_property(dev.obj, '.Block', 'IdType')
            fstype.assertEqual('btrfs')

            # check '.Filesystem.BTRFS' properties
            dbus_label = self.get_property(dev.obj, '.Filesystem.BTRFS', 'label')
            _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % dev.path)
            dbus_label.assertEqual('test_raid1')
            dbus_label.assertEqual(sys_label)

            dbus_uuid = self.get_property(dev.obj, '.Filesystem.BTRFS', 'uuid')
            _ret, sys_uuid = self.run_command('lsblk -d -no UUID %s' % dev.path)
            dbus_uuid.assertEqual(sys_uuid)

            dbus_devs = self.get_property(dev.obj, '.Filesystem.BTRFS', 'num_devices')
            dbus_devs.assertEqual(2)

            # check data and metadata raid level
            with self._temp_mount(dev.path) as mnt:
                _ret, out = self.run_command('btrfs filesystem df %s' % mnt)
                self.assertIn('Data, RAID1:', out)
                self.assertIn('Metadata, RAID1:', out)

    def test_subvolumes(self):

        btrfs_version = self._get_btrfs_version()
        if btrfs_version == LooseVersion('4.13.2'):
            self.skipTest('subvolumes list is broken with btrfs-progs v4.13.2')

        dev = self._get_devices(1)[0]
        self.addCleanup(self._clean_format, dev.obj)

        manager = self.get_object('/Manager')
        manager.CreateVolume([dev.obj_path],
                             'test_subvols', 'single', 'single',
                             self.no_options,
                             dbus_interface=self.iface_prefix + '.Manager.BTRFS')
        self.write_file("/sys/block/%s/uevent" % dev.name, "change\n")

        fstype = self.get_property(dev.obj, '.Block', 'IdType')
        fstype.assertEqual('btrfs')

        # not mounted, should fail
        msg = 'org.freedesktop.UDisks2.Error.NotMounted: Volume not mounted'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            dev.obj.CreateSubvolume('test_sub1', self.no_options,
                                    dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')

        with self._temp_mount(dev.path) as mnt:
            # create a subvolume
            dev.obj.CreateSubvolume('test_sub1', self.no_options,
                                    dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')
            _ret, out = self.run_command('btrfs subvolume list %s' % mnt)
            name = out.strip().split(' ')[-1]
            self.assertEqual(name, 'test_sub1')

            # list all subvolumes
            subs, num = dev.obj.GetSubvolumes(False, self.no_options,
                                              dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')
            self.assertEqual(num, 1)
            self.assertEqual(subs[0][2], 'test_sub1')  # name (path) is the third item

            # delete the subvolume
            dev.obj.RemoveSubvolume('test_sub1', self.no_options,
                                    dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')
            _ret, out = self.run_command('btrfs subvolume list %s' % mnt)
            self.assertFalse(out)  # no subvolume -> empty output

            # list all subvolumes
            _subs, num = dev.obj.GetSubvolumes(False, self.no_options,
                                               dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')
            self.assertEqual(num, 0)

    def test_add_remove_device(self):
        dev1, dev2 = self._get_devices(2)
        self.addCleanup(self._clean_format, dev1.obj)
        self.addCleanup(self._clean_format, dev2.obj)

        manager = self.get_object('/Manager')
        manager.CreateVolume([dev1.obj_path],
                             'test_add_remove', 'single', 'single',
                             self.no_options,
                             dbus_interface=self.iface_prefix + '.Manager.BTRFS')
        with open("/sys/block/%s/uevent" % dev1.name, "w") as f:
            f.write("change\n")

        fstype = self.get_property(dev1.obj, '.Block', 'IdType')
        fstype.assertEqual('btrfs')

        # shouldn't be possible to remove only device
        with self._temp_mount(dev1.path):
            msg = 'unable to remove the only writeable device'
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                dev1.obj.RemoveDevice(dev1.obj_path, self.no_options,
                                      dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')

        # add second device to the volume
        with self._temp_mount(dev1.path):
            dev1.obj.AddDevice(dev2.obj_path, self.no_options,
                               dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')
        with open("/sys/block/%s/uevent" % dev2.name, "w") as f:
            f.write("change\n")

        # check filesystem type of the new device
        fstype = self.get_property(dev2.obj, '.Block', 'IdType')
        fstype.assertEqual('btrfs')

        # check number of devices
        dbus_devs = self.get_property(dev1.obj, '.Filesystem.BTRFS', 'num_devices')
        dbus_devs.assertEqual(2)

        _ret, out = self.run_command('btrfs filesystem show %s' % dev1.path)
        self.assertIn('Total devices 2 FS', out)
        self.assertIn('path %s' % dev1.path, out)
        self.assertIn('path %s' % dev2.path, out)

        # remove the second device
        with self._temp_mount(dev1.path):
            dev1.obj.RemoveDevice(dev2.obj_path, self.no_options,
                                  dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')
            fstype = self.get_property(dev2.obj, '.Block', 'IdType')
            fstype.assertFalse()

        # check number of devices
        # XXX: udisks currently reports wrong number of devices (2)
        # 'handle_remove_device' sets the property to '1' but after unmounting
        # the device 'on_mount_monitor_mount_removed' triggers another properties
        # update and this sets the property to '2' for some unknown reason
        # dbus_devs = self.get_property(dev1.obj, '.Filesystem.BTRFS', 'num_devices')
        # with self.assertRaises(AssertionError):
        #     dbus_devs.assertEqual(1)

        _ret, out = self.run_command('btrfs filesystem show %s' % dev1.path)
        self.assertIn('Total devices 1 FS', out)
        self.assertIn('path %s' % dev1.path, out)
        self.assertNotIn('path %s' % dev2.path, out)

    def test_snapshot(self):
        dev = self._get_devices(1)[0]
        self.addCleanup(self._clean_format, dev.obj)

        manager = self.get_object('/Manager')
        manager.CreateVolume([dev.obj_path],
                             'test_snapshot', 'single', 'single',
                             self.no_options,
                             dbus_interface=self.iface_prefix + '.Manager.BTRFS')
        self.write_file("/sys/block/%s/uevent" % dev.name, "change\n")
        fstype = self.get_property(dev.obj, '.Block', 'IdType')
        fstype.assertEqual('btrfs')

        with self._temp_mount(dev.path) as mnt:
            # create a subvolume
            dev.obj.CreateSubvolume('test_sub1', self.no_options,
                                    dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')

            # create snapshot
            dev.obj.CreateSnapshot('test_sub1', 'test_sub1_snapshot', False, self.no_options,
                                   dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')

            # list snapshots
            snaps, num = dev.obj.GetSubvolumes(True, self.no_options,
                                               dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')
            self.assertEqual(num, 1)
            self.assertEqual(snaps[0][2], 'test_sub1_snapshot')  # name (path) is the third item

            _ret, out = self.run_command('btrfs subvolume list %s' % mnt)
            self.assertIn('path test_sub1', out)
            self.assertIn('path test_sub1_snapshot', out)

    def test_resize(self):
        dev = self._get_devices(1)[0]
        self.addCleanup(self._clean_format, dev.obj)

        manager = self.get_object('/Manager')
        manager.CreateVolume([dev.obj_path],
                             'test_snapshot', 'single', 'single',
                             self.no_options,
                             dbus_interface=self.iface_prefix + '.Manager.BTRFS')
        self.write_file("/sys/block/%s/uevent" % dev.name, "change\n")
        fstype = self.get_property(dev.obj, '.Block', 'IdType')
        fstype.assertEqual('btrfs')

        with self._temp_mount(dev.path) as mnt:
            new_size = dev.size - 20 * 1024**2
            dev.obj.Resize(dbus.UInt64(new_size), self.no_options,
                           dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')

            _ret, out = self.run_command('btrfs filesystem show %s' % mnt)
            m = re.search(r'.*devid +1 size (.+) used', out, flags=re.DOTALL)
            sys_size = bytesize.Size(m.group(1))
            self.assertEqual(sys_size.convert_to(bytesize.B), new_size)

    def test_label(self):
        dev = self._get_devices(1)[0]
        self.addCleanup(self._clean_format, dev.obj)

        manager = self.get_object('/Manager')
        manager.CreateVolume([dev.obj_path],
                             'test_label', 'single', 'single',
                             self.no_options,
                             dbus_interface=self.iface_prefix + '.Manager.BTRFS')
        self.write_file("/sys/block/%s/uevent" % dev.name, "change\n")
        fstype = self.get_property(dev.obj, '.Block', 'IdType')
        fstype.assertEqual('btrfs')

        dev.obj.SetLabel('new_label', self.no_options,
                         dbus_interface=self.iface_prefix + '.Filesystem.BTRFS')

        self.write_file("/sys/block/%s/uevent" % dev.name, "change\n")
        self.udev_settle()
        dbus_label = self.get_property(dev.obj, '.Filesystem.BTRFS', 'label')
        dbus_label.assertEqual('new_label')

        _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % dev.path)
        self.assertEqual(sys_label, 'new_label')
