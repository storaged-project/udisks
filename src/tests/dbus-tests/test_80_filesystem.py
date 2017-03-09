import dbus
import os
import tempfile

import udiskstestcase


class UdisksFSTestCase(udiskstestcase.UdisksTestCase):
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

    @classmethod
    def module_loaded(cls, module):
        ret, _out = cls.run_command('lsmod | grep %s' % module)
        return ret == 0

    def test_create_format(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_name)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_name, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)

        # test dbus properties
        usage = self.get_property(disk, '.Block', 'IdUsage')
        usage.assertEqual('filesystem')

        fstype = self.get_property(disk, '.Block', 'IdType')
        fstype.assertEqual(self._fs_name)

        # test system values
        _ret, sys_fstype = self.run_command('lsblk -d -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_fstype, self._fs_name)

    def _invalid_label(self, disk):
        pass

    def test_label(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_name)

        if not self._can_label:
            self.skipTest('Cannot set label on %s filesystem' % self._fs_name)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem with label
        label = 'test'
        d = dbus.Dictionary(signature='sv')
        d['label'] = label
        disk.Format(self._fs_name, d, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)

        # test dbus properties
        dbus_label = self.get_property(disk, '.Block', 'IdLabel')
        dbus_label.assertEqual(label)

        # test system values
        _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % self.vdevs[0])
        self.assertEqual(sys_label, label)

        # change the label
        label = 'AAAA' if self._fs_name == 'vfat' else 'aaaa'  # XXX udisks changes vfat labels to uppercase
        disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

        # test dbus properties
        dbus_label = self.get_property(disk, '.Block', 'IdLabel')
        dbus_label.assertEqual(label)

        # test system values
        _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % self.vdevs[0])
        self.assertEqual(sys_label, label)

        # test invalid label behaviour
        self._invalid_label(disk)

    def test_mount_auto(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_name)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_name)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_name, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)

        # not mounted
        mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        mounts.assertLen(0)

        # mount
        d = dbus.Dictionary(signature='sv')
        d['fstype'] = self._fs_name
        d['options'] = 'ro'
        mnt_path = disk.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self._unmount, self.vdevs[0])

        # dbus mountpoint
        dbus_mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        dbus_mnt = self.ay_to_str(dbus_mounts.value[0])  # mountpoints are arrays of bytes
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
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_name)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_name)

        # this test will change /etc/fstab, we might want to revert the changes after it finishes
        fstab = self.read_file('/etc/fstab')
        self.addCleanup(self.write_file, '/etc/fstab', fstab)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_name, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)

        # create a tempdir
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)

        # configuration items as arrays of dbus.Byte
        mnt = self.str_to_ay(tmp.name)
        fstype = self.str_to_ay(self._fs_name)
        opts = self.str_to_ay('ro')

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
        dbus_mounts.assertLen(1)  # just one mountpoint
        dbus_mnt = self.ay_to_str(dbus_mounts.value[0])  # mountpoints are arrays of bytes
        self.assertEqual(dbus_mnt, tmp.name)

        # system mountpoint
        self.assertTrue(os.path.ismount(tmp.name))

        _ret, out = self.run_command('mount | grep %s' % self.vdevs[0])
        self.assertIn(tmp.name, out)
        self.assertIn('ro', out)


class Ext2TestCase(UdisksFSTestCase):
    _fs_name = 'ext2'
    _can_create = True and UdisksFSTestCase.command_exists('mke2fs')
    _can_label = True and UdisksFSTestCase.command_exists('tune2fs')
    _can_mount = True

    def _invalid_label(self, disk):
        label = 'a' * 17  # at most 16 characters, longer should be truncated
        disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

        # test dbus properties
        dbus_label = self.get_property(disk, '.Block', 'IdLabel')
        dbus_label.assertEqual(label[0:16])

        # test system values
        _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % self.vdevs[0])
        self.assertEqual(sys_label, label[0:16])


class Ext3TestCase(Ext2TestCase):
    _fs_name = 'ext3'

    def _invalid_label(self, disk):
        pass


class Ext4TestCase(Ext2TestCase):
    _fs_name = 'ext4'

    def _invalid_label(self, disk):
        pass


class XFSTestCase(UdisksFSTestCase):
    _fs_name = 'xfs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.xfs')
    _can_label = True and UdisksFSTestCase.command_exists('xfs_admin')
    _can_mount = True

    def _invalid_label(self, disk):
        label = 'a a'  # space not allowed
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error setting label'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')


class VFATTestCase(UdisksFSTestCase):
    _fs_name = 'vfat'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.vfat')
    _can_label = True and UdisksFSTestCase.command_exists('fatlabel')
    _can_mount = True

    def _invalid_label(self, disk):
        label = 'a' * 12  # at most 11 characters
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error setting label'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')


class NTFSTestCase(UdisksFSTestCase):
    _fs_name = 'ntfs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.ntfs')
    _can_label = True and UdisksFSTestCase.command_exists('ntfslabel')
    _can_mount = True


class BTRFSTestCase(UdisksFSTestCase):
    _fs_name = 'btrfs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.btrfs')
    _can_label = True and UdisksFSTestCase.command_exists('btrfs')
    _can_mount = True


class ReiserFSTestCase(UdisksFSTestCase):
    _fs_name = 'reiserfs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.reiserfs')
    _can_label = True and UdisksFSTestCase.command_exists('reiserfstune')
    _can_mount = True


class MinixTestCase(UdisksFSTestCase):
    _fs_name = 'minix'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.minix')
    _can_label = False
    _can_mount = True and UdisksFSTestCase.module_loaded('minix')


class NILFS2TestCase(UdisksFSTestCase):
    _fs_name = 'nilfs2'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.nilfs2')
    _can_label = True and UdisksFSTestCase.command_exists('nilfs-tune')
    _can_mount = True and UdisksFSTestCase.module_loaded('nilfs2')


class F2FSTestCase(UdisksFSTestCase):
    _fs_name = 'f2fs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.f2fs')
    _can_label = False
    _can_mount = True and UdisksFSTestCase.module_loaded('f2fs')


class FailsystemTestCase(UdisksFSTestCase):
    # test that not supported operations fail 'nicely'

    def test_create_format(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # try to create some nonexisting filesystem
        msg = 'org.freedesktop.UDisks2.Error.NotSupported: Creation of file system '\
              'type definitely-nonexisting-fs is not supported'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.Format('definitely-nonexisting-fs', self.no_options,
                        dbus_interface=self.iface_prefix + '.Block')

    def test_label(self):
        # we need some filesystem that doesn't support labels
        fs = MinixTestCase

        if not fs._can_create:
            self.skipTest('Cannot create %s filesystem to test not supported '
                          'labelling.' % fs._fs_name)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # try create minix filesystem with label
        label = 'test'
        d = dbus.Dictionary(signature='sv')
        d['label'] = label

        msg = 'org.freedesktop.UDisks2.Error.NotSupported: File system '\
              'type %s does not support labels' % fs._fs_name
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.Format(fs._fs_name, d, dbus_interface=self.iface_prefix + '.Block')

        # create minix filesystem without label and try to set it later
        disk.Format(fs._fs_name, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)

        msg = 'org.freedesktop.UDisks2.Error.NotSupported: Don\'t know how to '\
              'change label on device of type filesystem:%s' % fs._fs_name
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.SetLabel('test', self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

    def test_mount_auto(self):
        # we need some mountable filesystem, ext4 should do the trick
        fs = Ext4TestCase

        if not fs._can_create:
            self.skipTest('Cannot create %s filesystem to test not supported '
                          'mount options.' % fs._fs_name)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        disk.Format(fs._fs_name, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._clean_format, disk)
        self.addCleanup(self._unmount, self.vdevs[0])  # paranoid cleanup

        # wrong fstype
        d = dbus.Dictionary(signature='sv')
        d['fstype'] = 'xfs'

        msg = '[Ww]rong fs type'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            mnt_path = disk.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
            self.assertIsNone(mnt_path)

        # invalid option
        d = dbus.Dictionary(signature='sv')
        d['fstype'] = fs._fs_name
        d['options'] = 'definitely-nonexisting-option'

        msg = 'org.freedesktop.UDisks2.Error.OptionNotPermitted: Mount option '\
              '`definitely-nonexisting-option\' is not allowed'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            mnt_path = disk.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
            self.assertIsNone(mnt_path)

        # should not be mounted -- so lets try to unmount it
        msg = 'org.freedesktop.UDisks2.Error.NotMounted: Device `%s\' is not '\
              'mounted' % self.vdevs[0]
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

    def test_mount_fstab(self):
        pass


del UdisksFSTestCase  # skip UdisksFSTestCase
