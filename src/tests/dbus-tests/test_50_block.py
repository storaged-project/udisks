import copy
import dbus
import fcntl
import os
import time

import udiskstestcase


class UdisksBlockTest(udiskstestcase.UdisksTestCase):
    '''This is a basic block device test suite'''

    def _clean_format(self, disk_path):
        self.run_command('wipefs -a %s' % disk_path)

    def _close_luks(self, disk):
        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

    def test_format(self):

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create xfs filesystem
        disk.Format('xfs', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        usage = self.get_property(disk, '.Block', 'IdUsage')
        usage.assertEqual('filesystem')

        fstype = self.get_property(disk, '.Block', 'IdType')
        fstype.assertEqual('xfs')

        _ret, sys_fstype = self.run_command('lsblk -d -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_fstype, 'xfs')

        # remove the format
        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        disk.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

        # check if the disk is empty
        usage = self.get_property(disk, '.Block', 'IdUsage')
        usage.assertEqual('')

        fstype = self.get_property(disk, '.Block', 'IdType')
        fstype.assertEqual('')

        _ret, sys_fstype = self.run_command('lsblk -d -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_fstype, '')

    def test_format_parttype(self):

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create partition table first
        disk.Format('dos', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        # now create partition
        path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(100 * 1024**2),
                                    '', '', self.no_options,
                                    dbus_interface=self.iface_prefix + '.PartitionTable')

        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        # now format it to swap with 'update-partition-type'
        d = dbus.Dictionary(signature='sv')
        d['update-partition-type'] = True
        part.Format('swap', d, dbus_interface=self.iface_prefix + '.Block')

        # part type should be set to swap (0x42 or 0x82)
        dbus_type = self.get_property(part, '.Partition', 'Type')
        dbus_type.assertIn(['0x42', '0x82'])

        part_name = str(part.object_path).split('/')[-1]
        _ret, sys_type = self.run_command('blkid /dev/%s -p -o value -s PART_ENTRY_TYPE' % part_name)
        self.assertIn(sys_type, ['0x42', '0x82'])

    def test_open(self):

        # O_ACCMODE is node defined in Python 2 version of 'os' module
        try:
            from os import O_ACCMODE
        except ImportError:
            O_ACCMODE = 3

        # format the disk
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        disk.Format('xfs', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        self.addCleanup(self._clean_format, self.vdevs[0])

        # OpenForBackup
        dbus_fd = disk.OpenForBackup(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(dbus_fd)

        fd = dbus_fd.take()
        mode = fcntl.fcntl(fd, fcntl.F_GETFL) & O_ACCMODE
        self.assertEqual(mode, os.O_RDONLY)
        os.close(fd)

        # OpenForRestore
        dbus_fd = disk.OpenForRestore(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(dbus_fd)

        fd = dbus_fd.take()
        mode = fcntl.fcntl(fd, fcntl.F_GETFL) & O_ACCMODE
        self.assertEqual(mode, os.O_WRONLY)
        os.close(fd)

        # OpenForBenchmark
        dbus_fd = disk.OpenForBenchmark(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(dbus_fd)

        fd = dbus_fd.take()
        mode = fcntl.fcntl(fd, fcntl.F_GETFL)
        self.assertTrue(bool(mode & os.O_DIRECT))
        self.assertTrue(bool(mode & os.O_SYNC))
        os.close(fd)

        # OpenDevice
        with self.assertRaises(dbus.exceptions.DBusException):  # invalid mode
            disk.OpenDevice('abc', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        d = dbus.Dictionary(signature='sv')
        d['flags'] = os.O_RDWR
        with self.assertRaises(dbus.exceptions.DBusException):  # read-only mode with O_RDWR flag
            disk.OpenDevice('r', d, dbus_interface=self.iface_prefix + '.Block')

        d = dbus.Dictionary(signature='sv')
        d['flags'] = os.O_ASYNC | os.O_DIRECT
        dbus_fd = disk.OpenDevice('w', d, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(dbus_fd)

        fd = dbus_fd.take()
        mode = fcntl.fcntl(fd, fcntl.F_GETFL)
        self.assertTrue(bool(mode & os.O_WRONLY))
        self.assertTrue(bool(mode & os.O_DIRECT))
        self.assertTrue(bool(mode & os.O_ASYNC))
        os.close(fd)

    def test_configuration_fstab(self):

        # this test will change /etc/fstab, we might want to revert the changes when it finishes
        fstab = self.read_file('/etc/fstab')
        self.addCleanup(self.write_file, '/etc/fstab', fstab)

        # format the disk
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        disk.Format('xfs', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        # cleanup -- remove format
        self.addCleanup(self._clean_format, self.vdevs[0])

        # configuration items as arrays of dbus.Byte
        mnt = self.str_to_ay('/mnt/test')
        fstype = self.str_to_ay('xfs')
        opts = self.str_to_ay('defaults')

        # set the new configuration
        conf = dbus.Dictionary({'dir': mnt, 'type': fstype, 'opts': opts, 'freq': 0, 'passno': 0},
                               signature=dbus.Signature('sv'))
        disk.AddConfigurationItem(('fstab', conf), self.no_options,
                                  dbus_interface=self.iface_prefix + '.Block')

        # get the configuration
        old_conf = self.get_property(disk, '.Block', 'Configuration')
        old_conf.assertTrue()
        self.assertEqual(old_conf.value[0][1]['dir'], mnt)
        self.assertEqual(old_conf.value[0][1]['type'], fstype)
        self.assertEqual(old_conf.value[0][1]['opts'], opts)
        self.assertEqual(old_conf.value[0][1]['passno'], 0)
        self.assertEqual(old_conf.value[0][1]['freq'], 0)

        # update the configuration
        new_opts = self.str_to_ay('defaults,noauto')
        new_conf = copy.deepcopy(old_conf.value)
        new_conf[0][1]['opts'] = new_opts

        disk.UpdateConfigurationItem((old_conf.value[0][0], old_conf.value[0][1]), (new_conf[0][0], new_conf[0][1]),
                                     self.no_options, dbus_interface=self.iface_prefix + '.Block')

        # get the configuration after the update
        upd_conf = self.get_property(disk, '.Block', 'Configuration')
        upd_conf.assertTrue()
        upd_conf.assertEqual(new_opts, getter=lambda c: c[0][1]['opts'])

        # remove the configuration
        disk.RemoveConfigurationItem((upd_conf.value[0][0], upd_conf.value[0][1]),
                                     self.no_options, dbus_interface=self.iface_prefix + '.Block')

        upd_conf = self.get_property(disk, '.Block', 'Configuration')
        upd_conf.assertFalse()

    def test_configuration_crypttab(self):

        # this test will change /etc/crypttab, we might want to revert the changes when it finishes
        crypttab = self.read_file('/etc/crypttab')
        self.addCleanup(self.write_file, '/etc/crypttab', crypttab)

        # format the disk
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        disk.Format('xfs', {'encrypt.passphrase': 'test'}, dbus_interface=self.iface_prefix + '.Block')

        # cleanup -- close the luks and remove format
        self.addCleanup(self._clean_format, self.vdevs[0])
        self.addCleanup(self._close_luks, disk)

        # configuration items as arrays of dbus.Byte
        opts = self.str_to_ay('verify')
        passwd = self.str_to_ay('test')

        # set the new configuration
        conf = dbus.Dictionary({'passphrase-contents': passwd,
                                'options': opts}, signature=dbus.Signature('sv'))
        disk.AddConfigurationItem(('crypttab', conf), self.no_options, dbus_interface=self.iface_prefix + '.Block')

        # get the configuration
        old_conf = self.get_property(disk, '.Block', 'Configuration')
        old_conf.assertTrue()
        self.assertEqual(old_conf.value[0][1]['options'], opts)

        # get the secret configuration (passphrase)
        sec_conf = disk.GetSecretConfiguration(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(sec_conf)
        self.assertEqual(sec_conf[0][1]['passphrase-contents'], passwd)

        # update the configuration
        new_opts = self.str_to_ay('verify,discard')
        new_conf = copy.deepcopy(sec_conf)
        new_conf[0][1]['options'] = new_opts

        disk.UpdateConfigurationItem((sec_conf[0][0], sec_conf[0][1]), (new_conf[0][0], new_conf[0][1]),
                                     self.no_options, dbus_interface=self.iface_prefix + '.Block')

        # get the configuration after the update
        upd_conf = self.get_property(disk, '.Block', 'Configuration')
        upd_conf.assertTrue()
        upd_conf.assertEqual(new_opts, getter=lambda c: c[0][1]['options'])

        # remove the configuration
        disk.RemoveConfigurationItem((upd_conf.value[0][0], upd_conf.value[0][1]),
                                     self.no_options, dbus_interface=self.iface_prefix + '.Block')

        upd_conf = self.get_property(disk, '.Block', 'Configuration')
        upd_conf.assertFalse()

    def test_rescan(self):

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        disk.Rescan(self.no_options, dbus_interface=self.iface_prefix + '.Block')
