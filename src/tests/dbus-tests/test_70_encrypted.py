import dbus
import os
import re
import six
import shutil
import tarfile
import tempfile
import time
import unittest
import configparser

import gi
gi.require_version('BlockDev', '2.0')
from gi.repository import BlockDev

from distutils.version import LooseVersion

import udiskstestcase


UDISKS_CONFIG_FILE = "/etc/udisks2/udisks2.conf"


def _get_cryptsetup_version():
    _ret, out = udiskstestcase.UdisksTestCase.run_command('cryptsetup --version')
    m = re.search(r'cryptsetup ([\d\.]+)', out)
    if not m or len(m.groups()) != 1:
        raise RuntimeError('Failed to determine cryptsetup version from: %s' % out)
    return LooseVersion(m.groups()[0])


def _get_blkid_version():
    _ret, out = udiskstestcase.UdisksTestCase.run_command('blkid --version')
    m = re.search(r'blkid from util-linux ([\d\.]+)', out)
    if not m or len(m.groups()) != 1:
        raise RuntimeError('Failed to determine blkid version from: %s' % out)
    return LooseVersion(m.groups()[0])


class UdisksEncryptedTest(udiskstestcase.UdisksTestCase):
    '''This is an encrypted device test suite'''

    def _create_luks(self, device, passphrase, binary=False):
        raise NotImplementedError()

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

        dbus_version = self.get_property(disk, '.Block', 'IdVersion')
        dbus_version.assertEqual(self.luks_version)

        device = self.get_property(disk, '.Block', 'Device')
        device.assertEqual(self.str_to_ay(self.vdevs[0]))  # device is an array of byte

        # check the size of the LUKS metadata
        metadata_size = self.get_property(disk, '.Encrypted', 'MetadataSize')

        dumped_metadata_size = self._get_metadata_size_from_dump(self.vdevs[0])
        self.assertEqual(int(metadata_size.value), dumped_metadata_size,
                         "LUKS metadata size differs (DBus value != cryptsetup luksDump)")

        # check system values
        _ret, sys_type = self.run_command('lsblk -d -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_type, 'crypto_LUKS')

        _ret, sys_uuid = self.run_command('lsblk -d -no UUID %s' % self.vdevs[0])
        dbus_uuid = self.get_property(disk, '.Block', 'IdUUID')
        dbus_uuid.assertEqual(sys_uuid)

        # get the luks device
        _ret, dm_name = self.run_command('ls /sys/block/%s/holders/' % disk_name)
        obj_name = 'dm_2d' + dm_name[3:]  # '-' is encoded as '_2d' in object paths
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
        obj_name = 'dm_2d' + dm_name[3:]  # '-' is encoded as '_2d' in object paths
        luks = self.get_object('/block_devices/' + obj_name)
        self.assertIsNotNone(luks)

        _ret, luks_uuid = self.run_command('lsblk -d -no UUID /dev/%s' % dm_name)
        self.assertTrue(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        dbus_cleartext = self.get_property(disk, '.Encrypted', 'CleartextDevice')
        dbus_cleartext.assertEqual(self.path_prefix +'/block_devices/' + obj_name)

        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertFalse(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        dbus_cleartext = self.get_property(disk, '.Encrypted', 'CleartextDevice')
        dbus_cleartext.assertEqual('/')

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

        dbus_cleartext = self.get_property(disk, '.Encrypted', 'CleartextDevice')
        dbus_cleartext.assertEqual(luks)

        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertFalse(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        dbus_cleartext = self.get_property(disk, '.Encrypted', 'CleartextDevice')
        dbus_cleartext.assertEqual('/')

        # read-only
        ro_opts = dbus.Dictionary({'read-only': dbus.Boolean(True)}, signature=dbus.Signature('sv'))
        luks = disk.Unlock('test', ro_opts,
                           dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertIsNotNone(luks)
        self.assertTrue(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        luks_obj = self.get_object(luks)
        self.assertIsNotNone(luks_obj)
        luks_ro = self.get_property(luks_obj, '.Block', 'ReadOnly')
        luks_ro.assertTrue()

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
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

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_open_crypttab_keyfile(self):
        # this test will change /etc/crypttab, we might want to revert the changes when it finishes
        crypttab = self.read_file('/etc/crypttab')
        self.addCleanup(self.write_file, '/etc/crypttab', crypttab)

        passwd = b'test\0test'
        luks_name = 'myshinylittleluks'

        # create key file
        _fd, key_file = tempfile.mkstemp()
        self.addCleanup(self.remove_file, key_file)

        self.write_file(key_file, passwd, binary=True)

        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('/block_devices/' + disk_name)

        self._create_luks(disk, passwd, binary=True)
        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        _ret, disk_uuid = self.run_command('lsblk -d -no UUID %s' % self.vdevs[0])

        # lock the device
        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

        # add new entry to the crypttab
        new_crypttab = crypttab + '%s UUID=%s %s\n' % (luks_name, disk_uuid, key_file)
        self.write_file('/etc/crypttab', new_crypttab)

        # give udisks time to react to change of the file
        time.sleep(5)
        dbus_conf = disk.GetSecretConfiguration(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(dbus_conf)
        self.assertEqual(self.ay_to_str(dbus_conf[0][1]['name']), luks_name)
        self.assertEqual(self.ay_to_str(dbus_conf[0][1]['passphrase-path']), key_file)

        # unlock the device using empty passphrase (should use the key file)
        luks = disk.Unlock('', self.no_options,
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
        obj_name = 'dm_2d' + dm_name[3:]  # '-' is encoded as '_2d' in object paths
        luks = self.get_object('/block_devices/' + obj_name)
        self.assertIsNotNone(luks)

        mnt_path = luks.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.assertIsNotNone(mnt_path)
        self.addCleanup(self.try_unmount, mnt_path)

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

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_configuration_track_parents(self):
        # this test will change /etc/crypttab and /etc/fstab, we might want to revert the changes
        # in case something goes wrong
        crypttab = self.read_file('/etc/crypttab')
        self.addCleanup(self.write_file, '/etc/crypttab', crypttab)
        fstab = self.read_file('/etc/fstab')
        self.addCleanup(self.write_file, '/etc/fstab', fstab)

        passphrase = 'test'

        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('/block_devices/' + disk_name)

        disk.Format('dos', self.no_options, dbus_interface=self.iface_prefix + '.Block')
        path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(100 * 1024**2), '', '',
                                    self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        # create LUKS on the partition and add it to crypttab
        self._create_luks(part, passphrase)
        self.udev_settle()

        conf = dbus.Dictionary({'name': self.str_to_ay('udisks_luks_test'),
                                'device': self.str_to_ay(self.vdevs[0] + '1'),
                                'passphrase-contents': self.str_to_ay(''),
                                'options': self.str_to_ay('')},
                               signature=dbus.Signature('sv'))
        part.AddConfigurationItem(('crypttab', conf), self.no_options,
                                  dbus_interface=self.iface_prefix + '.Block')

        luks_path = self.get_property(part, '.Encrypted', 'CleartextDevice')
        luks_dev = self.bus.get_object(self.iface_prefix, luks_path.value)
        self.assertIsNotNone(luks_dev)

        # format the LUKS device to ext4 and add it to fstab
        conf_items = dbus.Dictionary({'dir': self.str_to_ay('fakemountpoint'),
                                      'type': self.str_to_ay('ext4'),
                                      'opts': self.str_to_ay('defaults'),
                                      'freq': 0, 'passno': 0,
                                      'track-parents': True},
                                     signature=dbus.Signature('sv'))
        options = dbus.Dictionary({'config-items': dbus.Array([('fstab', conf_items)])}, signature=dbus.Signature('sv'))
        luks_dev.Format('ext4', options, dbus_interface=self.iface_prefix + '.Block')

        luks_uuid = self.get_property_raw(luks_dev, '.Block', 'IdUUID')

        # fstab configuration should be written to the ChildConfiguration of the partition device
        child_conf = self.get_property(part, '.Encrypted', 'ChildConfiguration')
        child_conf.assertTrue()
        self.assertEqual(child_conf.value[0][0], 'fstab')

        # wipe the LUKS partition
        # with tear-down this should close the LUKS and remove enries from fstab and crypttab
        options = dbus.Dictionary({'tear-down': True}, signature=dbus.Signature('sv'))
        part.Format('empty', options, dbus_interface=self.iface_prefix + '.Block')

        crypttab = self.read_file('/etc/crypttab')
        self.assertNotIn('udisks_luks_test', crypttab)

        fstab = self.read_file('/etc/fstab')
        self.assertNotIn(luks_uuid, fstab)


class UdisksEncryptedTestLUKS1(UdisksEncryptedTest):
    '''This is a LUKS1 encrypted device test suite'''

    luks_version = '1'

    def _create_luks(self, device, passphrase, binary=False):
        options = dbus.Dictionary(signature='sv')
        if binary:
            options['encrypt.passphrase'] = self.bytes_to_ay(passphrase)
            options['encrypt.type'] = 'luks1'
        else:
            options['encrypt.passphrase'] = passphrase
            options['encrypt.type'] = 'luks1'
        device.Format('xfs', options,
                      dbus_interface=self.iface_prefix + '.Block')

    def _get_metadata_size_from_dump(self, disk):
        ret, out = self.run_command("cryptsetup luksDump %s" % disk)
        if ret != 0:
            self.fail("Failed to get LUKS information from %s:\n%s" % (disk, out))

        m = re.search(r"Payload offset:\s*([0-9]+)", out)
        if m is None:
            self.fail("Failed to get LUKS 2 offset information using 'cryptsetup luksDump %s'" % disk)
        # offset value is in 512B blocks; we need to multiply to get the real metadata size
        return  int(m.group(1)) * 512

class UdisksEncryptedTestLUKS2(UdisksEncryptedTest):
    '''This is a LUKS2 encrypted device test suite'''

    luks_version = '2'
    requested_plugins = BlockDev.plugin_specs_from_names(("crypto",))

    @classmethod
    def setUpClass(cls):
        if not BlockDev.is_initialized():
            BlockDev.init(cls.requested_plugins, None)
        else:
            BlockDev.reinit(cls.requested_plugins, True, None)

        super(UdisksEncryptedTestLUKS2, cls).setUpClass()

    def _create_luks(self, device, passphrase, binary=False):
        options = dbus.Dictionary(signature='sv')
        if binary:
            options['encrypt.passphrase'] = self.bytes_to_ay(passphrase)
            options['encrypt.type'] = 'luks2'
        else:
            options['encrypt.passphrase'] = passphrase
            options['encrypt.type'] = 'luks2'
        device.Format('xfs', options,
                      dbus_interface=self.iface_prefix + '.Block')

    def _create_luks_integrity(self, device, passphrase):
        if not BlockDev.utils_have_kernel_module('dm-integrity'):
            self.skipTest('dm-integrity kernel module not available, skipping.')

        # UDisks doesn't support creating LUKS2 with integrity, we need to use libblockdev
        extra = BlockDev.CryptoLUKSExtra()
        extra.integrity = 'hmac(sha256)'

        BlockDev.crypto_luks_format(device, 'aes-cbc-essiv:sha256', 512, passphrase,
                                    None, 0, BlockDev.CryptoLUKSVersion.LUKS2, extra)

    def _get_metadata_size_from_dump(self, disk):
        ret, out = self.run_command("cryptsetup luksDump %s" % disk)
        if ret != 0:
            self.fail("Failed to get LUKS 2 information from '%s':\n%s" % (disk, out))

        m = re.search(r"offset:\s*([0-9]+)\s*\[bytes\]", out)
        if m is None:
            self.fail("Failed to get LUKS 2 offset information using 'cryptsetup luksDump %s'" % disk)
        return int(m.group(1))

    def _get_key_location(self, device):
        ret, out = self.run_command('cryptsetup status %s' % device)
        if ret != 0:
            self.fail('Failed to get key location from:\n%s' % out)

        m = re.search(r'\s*key location:\s*(\S+)\s*', out)
        if not m or len(m.groups()) != 1:
            self.fail('Failed to get key location from:\n%s' % out)

        return m.group(1)

    def setUp(self):
        cryptsetup_version = _get_cryptsetup_version()
        if cryptsetup_version < LooseVersion('2.0.0'):
            self.skipTest('LUKS2 not supported')

        super(UdisksEncryptedTestLUKS2, self).setUp()

    def test_resize(self):
        passwd = 'test'

        device = self.get_device(self.vdevs[0])
        self._create_luks(device, passwd)
        self.addCleanup(self._remove_luks, device)
        self.udev_settle()

        _ret, clear_dev = self.run_command('ls /sys/block/%s/holders/' % os.path.basename(self.vdevs[0]))
        self.assertEqual(_ret, 0)
        clear_size = self.get_block_size(clear_dev)

        # kernel keyring support and no passphrase for LUKS 2 given = fail
        if self._get_key_location('/dev/' + clear_dev) == 'keyring':
            msg = 'org.freedesktop.UDisks2.Error.Failed: Error resizing encrypted device /dev/dm-[0-9]+: Insufficient persmissions to resize device. *'
            with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                device.Resize(dbus.UInt64(100*1024*1024), self.no_options,
                              dbus_interface=self.iface_prefix + '.Encrypted')

        # wrong passphrase
        d = dbus.Dictionary(signature='sv')
        d['passphrase'] = 'wrongpassphrase'
        msg = 'org.freedesktop.UDisks2.Error.Failed: Error resizing encrypted device /dev/dm-[0-9]+: '\
              'Failed to activate device: (Operation not permitted|Incorrect passphrase)'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            device.Resize(dbus.UInt64(100*1024*1024), d,
                          dbus_interface=self.iface_prefix + '.Encrypted')

        # right passphrase
        d = dbus.Dictionary(signature='sv')
        d['passphrase'] = passwd
        device.Resize(dbus.UInt64(100*1024*1024), d,
                      dbus_interface=self.iface_prefix + '.Encrypted')

        clear_size2 = self.get_block_size(clear_dev)
        self.assertEqual(clear_size2, 100*1024*1024)

        # resize back to the original size (using binary passphrase)
        d = dbus.Dictionary(signature='sv')
        d['keyfile_contents'] = self.str_to_ay(passwd, False)
        device.Resize(dbus.UInt64(clear_size), d,
                      dbus_interface=self.iface_prefix + '.Encrypted')

        clear_size3 = self.get_block_size(clear_dev)
        self.assertEqual(clear_size3, clear_size)

    def _get_default_luks_version(self):
        manager = self.get_object('/Manager')
        default_encryption_type = self.get_property(manager, '.Manager', 'DefaultEncryptionType')
        return default_encryption_type.value[-1]

    def test_create_default(self):
        disk_name = os.path.basename(self.vdevs[0])
        disk = self.get_object('/block_devices/' + disk_name)

        # create LUKS without specifying version
        options = dbus.Dictionary(signature='sv')
        options['encrypt.passphrase'] = 'test'

        disk.Format('xfs', options,
                    dbus_interface=self.iface_prefix + '.Block')

        self.addCleanup(self._remove_luks, disk)
        self.udev_settle()

        default_version = self._get_default_luks_version()

        dbus_version = self.get_property(disk, '.Block', 'IdVersion')
        dbus_version.assertEqual(default_version)

    def test_suppported_encryption_types(self):
        manager = self.get_object('/Manager')
        supported_encryption_types = self.get_property(manager, '.Manager', 'SupportedEncryptionTypes')
        supported_encryption_types.assertLen(2)
        supported_encryption_types.assertContains("luks1")
        supported_encryption_types.assertContains("luks2")

    def test_default_encryption_type(self):
        if not os.path.exists(UDISKS_CONFIG_FILE):
            self.fail('UDisks config file not found.')

        config = configparser.ConfigParser()
        config.read(UDISKS_CONFIG_FILE)

        if 'defaults' not in config:
            self.fail('Failed to read defaults from UDisks config file.')

        manager = self.get_object('/Manager')
        default_encryption_type = self.get_property(manager, '.Manager', 'DefaultEncryptionType')
        default_encryption_type.assertEqual(config['defaults']['encryption'])

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_integrity(self):
        passwd = 'test'

        cryptsetup_version = _get_cryptsetup_version()
        if cryptsetup_version < LooseVersion('2.2.0'):
            self.skipTest('Integrity devices are not marked as internal in cryptsetup < 2.2.0')

        device = self.get_device(self.vdevs[0])
        self._create_luks_integrity(self.vdevs[0], passwd)

        self.addCleanup(self._remove_luks, device)
        self.udev_settle()

        self.assertHasIface(device, self.iface_prefix + '.Encrypted')

        # the device is not opened, we need to read the UUID from LUKS metadata
        luks_uuid = BlockDev.crypto_luks_uuid(self.vdevs[0])

        luks_path = device.Unlock('test', self.no_options,
                                  dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertIsNotNone(luks_path)
        self.assertTrue(os.path.exists('/dev/disk/by-uuid/%s' % luks_uuid))

        luks = self.bus.get_object(self.iface_prefix, luks_path)
        self.assertIsNotNone(luks)

        crypto_dev = self.get_property(luks, '.Block', 'CryptoBackingDevice')
        crypto_dev.assertEqual(device.object_path)

        dbus_cleartext = self.get_property(device, '.Encrypted', 'CleartextDevice')
        dbus_cleartext.assertEqual(luks_path)

        device.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

        dbus_cleartext = self.get_property(device, '.Encrypted', 'CleartextDevice')
        dbus_cleartext.assertEqual('/')


class UdisksEncryptedTestBITLK(udiskstestcase.UdisksTestCase):

    # we can't create BitLocker formats using libblockdev
    # so we are using these images from cryptsetup test suite
    # https://gitlab.com/cryptsetup/cryptsetup/blob/master/tests/bitlk-images.tar.xz
    bitlk_img = "bitlk-aes-xts-128.img"
    passphrase = "anaconda"
    tempdir = None

    def tearDown(self):
        try:
            self.loop.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
        except dbus.exceptions.DBusException as e:
            # ignore when the device is actually already locked
            if not str(e).endswith('is not unlocked'):
                raise e

        self.loop.Delete(self.no_options, dbus_interface=self.iface_prefix + '.Loop')
        shutil.rmtree(self.tempdir)

        super(UdisksEncryptedTestBITLK, self).tearDown()

    def setUp(self):
        cryptsetup_version = _get_cryptsetup_version()
        if cryptsetup_version < LooseVersion('2.3.0'):
            self.skipTest('BITLK not supported by cryptsetup')

        blkid_version = _get_blkid_version()
        if blkid_version < LooseVersion('2.33'):
            self.skipTest('BITLK not supported by blkid')

        self.manager = self.get_interface('/Manager', '.Manager')
        self.tempdir = tempfile.mkdtemp(prefix='udisks_test_bitlk')
        images = os.path.join(os.path.dirname(__file__), 'bitlk-images.tar.gz')
        with tarfile.open(images, 'r') as tar:
            tar.extractall(self.tempdir)

        with open(os.path.join(self.tempdir, self.bitlk_img), 'r+b') as loop_file:
            fd = loop_file.fileno()
            loop_path = self.manager.LoopSetup(fd, self.no_options)
            self.assertIsNotNone(loop_path)
            self.loop = self.bus.get_object(self.iface_prefix, loop_path)

        super(UdisksEncryptedTestBITLK, self).setUp()

    def test_open_close(self):
        self.assertHasIface(self.loop, self.iface_prefix + '.Encrypted')

        crypt_path = self.loop.Unlock(self.passphrase, self.no_options,
                                      dbus_interface=self.iface_prefix + '.Encrypted')
        self.assertIsNotNone(crypt_path)
        crypt_dev = self.bus.get_object(self.iface_prefix, crypt_path)
        self.assertIsNotNone(crypt_dev)

        dbus_cleartext = self.get_property(self.loop, '.Encrypted', 'CleartextDevice')
        dbus_cleartext.assertEqual(str(crypt_path))
        dbus_type = self.get_property(self.loop, '.Encrypted', 'HintEncryptionType')
        dbus_type.assertEqual("BITLK")

        dbus_backing = self.get_property(crypt_dev, '.Block', 'CryptoBackingDevice')
        dbus_backing.assertEqual(self.loop.object_path)
        dbus_fs = self.get_property(crypt_dev, '.Block', 'IdType')
        dbus_fs.assertEqual("ntfs")

        self.loop.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

        dbus_cleartext = self.get_property(self.loop, '.Encrypted', 'CleartextDevice')
        dbus_cleartext.assertEqual('/')

        # check that the DM crypt device disappears after lock
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(str(crypt_path), objects.keys())


del UdisksEncryptedTest  # skip UdisksEncryptedTest
