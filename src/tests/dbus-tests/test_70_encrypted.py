import dbus
import os
import six
import time
import unittest

import udiskstestcase


class UdisksEncryptedTest(udiskstestcase.UdisksTestCase):
    '''This is an encrypted device test suite'''

    def _create_luks(self, device, passphrase):
        device.Format('xfs', {'encrypt.passphrase': passphrase},
                      dbus_interface=self.iface_prefix + '.Block')

    def _remove_luks(self, device, close=True):
        if close:
            try:
                device.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
            except dbus.exceptions.DBusException as e:
                # ignore when luks is actually already locked
                if not str(e).endswith('is not unlocked'):
                    raise e

        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        device.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

    def _unmount(self, disk_path):
        self.run_command('umount %s' % disk_path)

    def test_create(self):
        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('/block_devices/' + disk_name)

        self._create_luks(disk, 'test')
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        # check dbus properties
        dbus_usage = self.get_property(disk, '.Block', 'IdUsage')
        dbus_usage.assertEqual('crypto')

        dbus_type = self.get_property(disk, '.Block', 'IdType')
        dbus_type.assertEqual('crypto_LUKS')

        device = self.get_property(disk, '.Block', 'Device')
        device.assertEqual(self.str_to_ay(self.vdevs[0]))  # device is an array of byte

        # check system values
        _ret, sys_type = self.run_command('lsblk -d -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_type, 'crypto_LUKS')

        _ret, sys_uuid = self.run_command('lsblk -d -no UUID %s' % self.vdevs[0])
        dbus_uuid = self.get_property(disk, '.Block', 'IdUUID')
        dbus_uuid.assertEqual(sys_uuid)

        # get the luks device
        _ret, dm_name = self.run_command('ls /sys/block/%s/holders/' % disk_name)
        obj_name = 'dm_2d' + dm_name[-1]  # '-' is encoded as '_2d' in object paths
        luks = self.get_object('/block_devices/' + obj_name)

        self.assertIsNotNone(luks)

        # check dbus properties
        dbus_usage = self.get_property(luks, '.Block', 'IdUsage')
        dbus_usage.assertEqual('filesystem')

        dbus_type = self.get_property(luks, '.Block', 'IdType')
        dbus_type.assertEqual('xfs')

        device = self.get_property(luks, '.Block', 'Device')
        device.assertEqual(self.str_to_ay('/dev/' + dm_name))  # device is an array of byte

        crypto_dev = self.get_property(luks, '.Block', 'CryptoBackingDevice')
        crypto_dev.assertEqual(disk.object_path)

        # check system values
        _ret, sys_type = self.run_command('lsblk -d -no FSTYPE /dev/%s' % dm_name)
        self.assertEqual(sys_type, 'xfs')

        _ret, sys_uuid = self.run_command('lsblk -d -no UUID /dev/%s' % dm_name)
        bus_uuid = self.get_property(luks, '.Block', 'IdUUID')
        bus_uuid.assertEqual(sys_uuid)

    def test_close_open(self):
        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('/block_devices/' + disk_name)

        self._create_luks(disk, 'test')
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        # get the uuid of the luks device
        _ret, dm_name = self.run_command('ls /sys/block/%s/holders/' % disk_name)
        obj_name = 'dm_2d' + dm_name[-1]  # '-' is encoded as '_2d' in object paths
        luks = self.get_object('/block_devices/' + obj_name)
        self.assertIsNotNone(luks)

        _ret, luks_uuid = self.run_command('lsblk -d -no UUID /dev/%s' % dm_name)
        self.assertTrue(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertFalse(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        # check that luks device disappears after lock
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(str(luks.object_path), objects.keys())

        # no password
        msg = 'org.freedesktop.UDisks2.Error.Failed: No key available.*'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.Unlock("", self.no_options,
                        dbus_interface=self.iface_prefix + '.Encrypted')

        # wrong password
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error unlocking %s *' % self.vdevs[0]
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.Unlock('shbdkjaf', self.no_options,
                        dbus_interface=self.iface_prefix + '.Encrypted')

        # right password
        luks = disk.Unlock('test', self.no_options,
                           dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertIsNotNone(luks)
        self.assertTrue(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

    @unittest.skipUnless("JENKINS_HOME" in os.environ, "skipping test that modifies system configuration")
    def test_open_crypttab(self):
        # this test will change /etc/crypttab, we might want to revert the changes when it finishes
        crypttab = self.read_file('/etc/crypttab')
        self.addCleanup(self.write_file, '/etc/crypttab', crypttab)

        passwd = 'test'
        luks_name = 'myshinylittleluks'

        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('/block_devices/' + disk_name)

        self._create_luks(disk, passwd)
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        _ret, disk_uuid = self.run_command('lsblk -d -no UUID %s' % self.vdevs[0])

        # lock the device
        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

        # add new entry to the crypttab
        new_crypttab = crypttab + '%s UUID=%s none\n' % (luks_name, disk_uuid)
        self.write_file('/etc/crypttab', new_crypttab)

        # give udisks time to react to change of the file
        time.sleep(5)
        dbus_conf = disk.GetSecretConfiguration(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(dbus_conf)
        self.assertEqual(self.ay_to_str(dbus_conf[0][1]['name']), luks_name)

        # unlock the device
        luks = disk.Unlock(passwd, self.no_options,
                           dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertIsNotNone(luks)

        # unlock should use name from crypttab for the /dev/mapper device
        dm_path = '/dev/mapper/%s' % luks_name
        self.assertTrue(os.path.exists(dm_path))

        # preferred 'device' should be /dev/mapper/name too
        luks_obj = self.get_object(luks)
        self.assertIsNotNone(luks_obj)
        luks_path = self.get_property(luks_obj, '.Block', 'PreferredDevice')
        luks_path.assertEqual(self.str_to_ay(dm_path))

    def test_mount(self):
        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('/block_devices/' + disk_name)

        self._create_luks(disk, 'test')
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        # get the luks object and mount it
        _ret, dm_name = self.run_command('ls /sys/block/%s/holders/' % disk_name)
        obj_name = 'dm_2d' + dm_name[-1]  # '-' is encoded as '_2d' in object paths
        luks = self.get_object('/block_devices/' + obj_name)
        self.assertIsNotNone(luks)

        mnt_path = luks.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.assertIsNotNone(mnt_path)
        self.addCleanup(self._unmount, mnt_path)

        # should not be possible to close mounted luks
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error locking'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

        # now unmount it and try to close it again
        luks.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

    def test_password_change(self):
        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('/block_devices/' + disk_name)

        self._create_luks(disk, 'test')
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        disk.ChangePassphrase('test', 'password', self.no_options,
                              dbus_interface=self.iface_prefix + '.Encrypted')

        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

        # old password, should fail
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error unlocking %s *' % self.vdevs[0]
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.Unlock('test', self.no_options,
                        dbus_interface=self.iface_prefix + '.Encrypted')

        # new password
        luks = disk.Unlock('password', self.no_options,
                           dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertIsNotNone(luks)

    def get_block_size(self, name):
        _ret, size_string = self.run_command('lsblk -n -b -o SIZE /dev/%s' % name)
        self.assertEqual(_ret, 0)
        return int(size_string)

    def test_resize(self):
        device = self.get_device(self.vdevs[0])
        self._create_luks(device, 'test')
        self.addCleanup(self._remove_luks, device)
        self.udev_settle()

        _ret, clear_dev = self.run_command('ls /sys/block/%s/holders/' % os.path.basename(self.vdevs[0]))
        self.assertEqual(_ret, 0)
        clear_size = self.get_block_size(clear_dev)

        device.Resize(dbus.UInt64(100*1024*1024), self.no_options,
                      dbus_interface=self.iface_prefix + '.Encrypted')

        clear_size2 = self.get_block_size(clear_dev)
        self.assertEqual(clear_size2, 100*1024*1024)

        device.Resize(dbus.UInt64(clear_size), self.no_options,
                      dbus_interface=self.iface_prefix + '.Encrypted')

        clear_size3 = self.get_block_size(clear_dev)
        self.assertEqual(clear_size3, clear_size)
