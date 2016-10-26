import dbus
import os

import storagedtestcase


class StoragedEncryptedTest(storagedtestcase.StoragedTestCase):
    '''This is an encrypted device test suite'''

    def _create_luks(self, device, passphrase):
        device.Format('xfs', {'encrypt.passphrase': passphrase},
                      dbus_interface=self.iface_prefix + '.Block')

    def _remove_luks(self, device, close=True):
        if close:
            device.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        device.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

    def test_create(self):
        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('', '/block_devices/' + disk_name)

        self._create_luks(disk, 'test')
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        # check dbus properties
        dbus_usage = self.get_property(disk, '.Block', 'IdUsage')
        self.assertEqual(dbus_usage, 'crypto')

        dbus_type = self.get_property(disk, '.Block', 'IdType')
        self.assertEqual(dbus_type, 'crypto_LUKS')

        device = self.get_property(disk, '.Block', 'Device')
        device_path = "".join([str(i) for i in device[:-1]])  # device is an array of byte
        self.assertEqual(device_path, self.vdevs[0])

        dbus_uuid = self.get_property(disk, '.Block', 'IdUUID')

        # check system values
        _ret, sys_type = self.run_command('lsblk -d -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_type, 'crypto_LUKS')

        _ret, sys_uuid = self.run_command('lsblk -d -no UUID %s' % self.vdevs[0])
        self.assertEqual(sys_uuid, dbus_uuid)

        # get the luks device
        _ret, dm_name = self.run_command('ls /sys/block/%s/holders/' % disk_name)
        obj_name = 'dm_2d' + dm_name[-1]  # '-' is encoded as '_2d' in object paths
        luks = self.get_object('', '/block_devices/' + obj_name)

        self.assertIsNotNone(luks)

        # check dbus properties
        dbus_usage = self.get_property(luks, '.Block', 'IdUsage')
        self.assertEqual(dbus_usage, 'filesystem')

        dbus_type = self.get_property(luks, '.Block', 'IdType')
        self.assertEqual(dbus_type, 'xfs')

        device = self.get_property(luks, '.Block', 'Device')
        device_path = "".join(str(i) for i in device[:-1])  # device is an array of byte
        self.assertEqual(device_path, '/dev/' + dm_name)

        dbus_uuid = self.get_property(luks, '.Block', 'IdUUID')

        crypto_dev = self.get_property(luks, '.Block', 'CryptoBackingDevice')
        self.assertEqual(crypto_dev, disk.object_path)

        # check system values
        _ret, sys_type = self.run_command('lsblk -d -no FSTYPE /dev/%s' % dm_name)
        self.assertEqual(sys_type, 'xfs')

        _ret, sys_uuid = self.run_command('lsblk -d -no UUID /dev/%s' % dm_name)
        self.assertEqual(sys_uuid, dbus_uuid)

    def test_close_open(self):
        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('', '/block_devices/' + disk_name)

        self._create_luks(disk, 'test')
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        # get the uuid of the luks device
        _ret, dm_name = self.run_command('ls /sys/block/%s/holders/' % disk_name)
        _ret, luks_uuid = self.run_command('lsblk -d -no UUID /dev/%s' % dm_name)
        self.assertTrue(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertFalse(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        # wrong password
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error unlocking %s *' % self.vdevs[0]
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.Unlock('shbdkjaf', self.no_options,
                        dbus_interface=self.iface_prefix + '.Encrypted')

        # right password
        luks = disk.Unlock('test', self.no_options,
                           dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertIsNotNone(luks)
        self.assertTrue(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

    def test_password_change(self):
        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('', '/block_devices/' + disk_name)

        self._create_luks(disk, 'test')
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        disk.ChangePassphrase('test', 'password', self.no_options,
                              dbus_interface=self.iface_prefix + '.Encrypted')

        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

        # old password, should fail
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error unlocking %s *' % self.vdevs[0]
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            disk.Unlock('test', self.no_options,
                        dbus_interface=self.iface_prefix + '.Encrypted')

        # new password
        luks = disk.Unlock('password', self.no_options,
                           dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertIsNotNone(luks)
