import dbus
import os
import tempfile
import time

import storagedtestcase


class StoragedFSTestCase(storagedtestcase.StoragedTestCase):
    _fs_name = None
    _can_create = False
    _can_label = False
    _can_mount = False

    def _clean_format(self, disk):
        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        disk.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

    def _unmount(self, disk_path):
        self.run_command('umount %s' % disk_path)

    @classmethod
    def command_exists(cls, command):
        ret, _out = cls.run_command('type %s' % command)
        return ret == 0

    def test_create_format(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_name)

        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_name, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)

        # test dbus properties
        usage = self.get_property(disk, '.Block', 'IdUsage')
        self.assertEqual(usage, 'filesystem')

        fstype = self.get_property(disk, '.Block', 'IdType')
        self.assertEqual(fstype, self._fs_name)

        # test system values
        _ret, sys_fstype = self.run_command('lsblk -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_fstype, self._fs_name)

    def _invalid_label(self, disk):
        pass

    def test_label(self):
        if not self._can_label:
            self.skipTest('Cannot set label on %s filesystem' % self._fs_name)

        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem with label
        label = 'test'
        d = dbus.Dictionary(signature='sv')
        d['label'] = label
        disk.Format(self._fs_name, d, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)
        time.sleep(1)

        # test dbus properties
        dbus_label = self.get_property(disk, '.Block', 'IdLabel')
        self.assertEqual(dbus_label, label)

        # test system values
        _ret, sys_label = self.run_command('lsblk -no LABEL %s' % self.vdevs[0])
        self.assertEqual(sys_label, label)

        # change the label
        label = 'AAAA' if self._fs_name == 'vfat' else 'aaaa'  # XXX storaged changes vfat labels to uppercase
        disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        time.sleep(1)

        # test dbus properties
        dbus_label = self.get_property(disk, '.Block', 'IdLabel')
        self.assertEqual(dbus_label, label)

        # test system values
        _ret, sys_label = self.run_command('lsblk -no LABEL %s' % self.vdevs[0])
        self.assertEqual(sys_label, label)

        # test invalid label behaviour
        self._invalid_label(disk)

    def test_mount_auto(self):
        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_name)

        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_name, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)

        # not mounted
        mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        self.assertListEqual(mounts, [])

        # mount
        d = dbus.Dictionary(signature='sv')
        d['fstype'] = self._fs_name
        d['options'] = 'ro'
        mnt_path = disk.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self._unmount, self.vdevs[0])

        # dbus mountpoint
        dbus_mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        self.assertEqual(len(dbus_mounts), 1)  # just one mountpoint
        dbus_mnt = "".join(str(i) for i in dbus_mounts[0][:-1])  # mountpoints are arrays of bytes
        self.assertEqual(dbus_mnt, mnt_path)

        # system mountpoint
        self.assertTrue(os.path.ismount(mnt_path))
        _ret, out = self.run_command('mount | grep %s' % self.vdevs[0])
        self.assertIn(mnt_path, out)
        self.assertIn('ro', out)

        # umount
        disk.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.assertFalse(os.path.ismount(mnt_path))

    def test_mount_fstab(self):
        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_name)

        # this test will change /etc/fstab, we might want to revert the changes after it finishes
        fstab = self.read_file('/etc/fstab')
        self.addCleanup(self.write_file, '/etc/fstab', fstab)

        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_name, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)

        # create a tempdir
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)

        # configuration items as arrays of dbus.Byte
        mnt = dbus.Array([dbus.Byte(ord(c)) for c in '%s\0' % tmp.name],
                         signature=dbus.Signature('y'), variant_level=1)
        fstype = dbus.Array([dbus.Byte(ord(c)) for c in '%s\0' % self._fs_name],
                            signature=dbus.Signature('y'), variant_level=1)
        opts = dbus.Array([dbus.Byte(ord(c)) for c in 'ro\0'],
                          signature=dbus.Signature('y'), variant_level=1)

        # set the new configuration
        conf = dbus.Dictionary({'dir': mnt, 'type': fstype, 'opts': opts, 'freq': 0, 'passno': 0},
                               signature=dbus.Signature('sv'))
        disk.AddConfigurationItem(('fstab', conf), self.no_options,
                                  dbus_interface=self.iface_prefix + '.Block')

        # mount using fstab options
        disk.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self._unmount, self.vdevs[0])

        # dbus mountpoint
        dbus_mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        self.assertEqual(len(dbus_mounts), 1)  # just one mountpoint
        dbus_mnt = "".join([str(i) for i in dbus_mounts[0][:-1]])  # mountpoints are arrays of bytes
        self.assertEqual(dbus_mnt, tmp.name)

        # system mountpoint
        self.assertTrue(os.path.ismount(tmp.name))

        _ret, out = self.run_command('mount | grep %s' % self.vdevs[0])
        self.assertIn(tmp.name, out)
        self.assertIn('ro', out)


class Ext2TestCase(StoragedFSTestCase):
    _fs_name = 'ext2'
    _can_create = True and StoragedFSTestCase.command_exists('mke2fs')
    _can_label = True and StoragedFSTestCase.command_exists('tune2fs')
    _can_mount = True

    def _invalid_label(self, disk):
        label = 'a' * 17  # at most 16 characters, longer should be truncated
        disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        time.sleep(1)

        # test dbus properties
        dbus_label = self.get_property(disk, '.Block', 'IdLabel')
        self.assertEqual(dbus_label, label[0:16])

        # test system values
        _ret, sys_label = self.run_command('lsblk -no LABEL %s' % self.vdevs[0])
        self.assertEqual(sys_label, label[0:16])


class Ext3TestCase(Ext2TestCase):
    _fs_name = 'ext3'

    def _invalid_label(self, disk):
        pass


class Ext4TestCase(Ext2TestCase):
    _fs_name = 'ext4'

    def _invalid_label(self, disk):
        pass


class XFSTestCase(StoragedFSTestCase):
    _fs_name = 'xfs'
    _can_create = True and StoragedFSTestCase.command_exists('mkfs.xfs')
    _can_label = True and StoragedFSTestCase.command_exists('xfs_admin')
    _can_mount = True

    def _invalid_label(self, disk):
        label = 'a a'  # space not allowed
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error setting label'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')


class VFATTestCase(StoragedFSTestCase):
    _fs_name = 'vfat'
    _can_create = True and StoragedFSTestCase.command_exists('mkfs.vfat')
    _can_label = True and StoragedFSTestCase.command_exists('fatlabel')
    _can_mount = True

    def _invalid_label(self, disk):
        label = 'a' * 12  # at most 11 characters
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error setting label'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')


class NTFSTestCase(StoragedFSTestCase):
    _fs_name = 'ntfs'
    _can_create = True and StoragedFSTestCase.command_exists('mkfs.ntfs')
    _can_label = True and StoragedFSTestCase.command_exists('ntfslabel')
    _can_mount = True


del StoragedFSTestCase  # skip StoragedFSTestCase
