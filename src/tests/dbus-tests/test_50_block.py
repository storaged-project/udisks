import copy
import dbus
import fcntl
import os
import time

import storagedtestcase


class StoragedBlockTest(storagedtestcase.StoragedTestCase):
    '''This is a basic block device test suite'''

    def _clean_format(self, disk):
        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        disk.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

    def _close_luks(self, disk):
        disk.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')

    def test_format(self):

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create xfs filesystem
        disk.Format('xfs', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        usage = self.get_property(disk, '.Block', 'IdUsage')
        self.assertEqual(usage, 'filesystem')

        fstype = self.get_property(disk, '.Block', 'IdType')
        self.assertEqual(fstype, 'xfs')

        _ret, sys_fstype = self.run_command('lsblk -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_fstype, 'xfs')

        # remove the format
        self._clean_format(disk)

        # check if the disk is empty
        usage = self.get_property(disk, '.Block', 'IdUsage')
        self.assertEqual(usage, '')

        fstype = self.get_property(disk, '.Block', 'IdType')
        self.assertEqual(fstype, '')

        _ret, sys_fstype = self.run_command('lsblk -no FSTYPE %s' % self.vdevs[0])
        self.assertEqual(sys_fstype, '')

    def test_open(self):

        # format the disk
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        disk.Format('xfs', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        self.addCleanup(self._clean_format, disk)

        # OpenForBackup
        dbus_fd = disk.OpenForBackup(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(dbus_fd)

        fd = dbus_fd.take()
        mode = fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_ACCMODE
        self.assertEqual(mode, os.O_RDONLY)
        os.close(fd)

        # OpenForRestore
        dbus_fd = disk.OpenForRestore(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(dbus_fd)

        fd = dbus_fd.take()
        mode = fcntl.fcntl(fd, fcntl.F_GETFL) & os.O_ACCMODE
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

    def test_configuration_fstab(self):

        # this test will change /etc/fstab, we might want to revert the changes when it finishes
        fstab = self.read_file('/etc/fstab')
        self.addCleanup(self.write_file, '/etc/fstab', fstab)

        # format the disk
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        disk.Format('xfs', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        # cleanup -- remove format
        self.addCleanup(self._clean_format, disk)

        # configuration items as arrays of dbus.Byte
        mnt = dbus.Array([dbus.Byte(ord(c)) for c in '/mnt/test\0'],
                         signature=dbus.Signature('y'), variant_level=1)
        fstype = dbus.Array([dbus.Byte(ord(c)) for c in 'xfs\0'],
                            signature=dbus.Signature('y'), variant_level=1)
        opts = dbus.Array([dbus.Byte(ord(c)) for c in 'defaults\0'],
                          signature=dbus.Signature('y'), variant_level=1)

        # set the new configuration
        conf = dbus.Dictionary({'dir': mnt, 'type': fstype, 'opts': opts, 'freq': 0, 'passno': 0},
                               signature=dbus.Signature('sv'))
        disk.AddConfigurationItem(('fstab', conf), self.no_options,
                                  dbus_interface=self.iface_prefix + '.Block')
        time.sleep(5)

        # get the configuration
        old_conf = self.get_property(disk, '.Block', 'Configuration')
        self.assertIsNotNone(old_conf)
        self.assertEqual(old_conf[0][1]['dir'], mnt)
        self.assertEqual(old_conf[0][1]['type'], fstype)
        self.assertEqual(old_conf[0][1]['opts'], opts)
        self.assertEqual(old_conf[0][1]['passno'], 0)
        self.assertEqual(old_conf[0][1]['freq'], 0)

        # update the configuration
        new_opts = dbus.Array([dbus.Byte(ord(c)) for c in 'defaults,noauto\0'],
                              signature=dbus.Signature('y'), variant_level=1)
        new_conf = copy.deepcopy(old_conf)
        new_conf[0][1]['opts'] = new_opts

        disk.UpdateConfigurationItem((old_conf[0][0], old_conf[0][1]), (new_conf[0][0], new_conf[0][1]),
                                     self.no_options, dbus_interface=self.iface_prefix + '.Block')
        time.sleep(5)

        # get the configuration after the update
        upd_conf = self.get_property(disk, '.Block', 'Configuration')
        self.assertIsNotNone(upd_conf)
        self.assertEqual(upd_conf[0][1]['opts'], new_opts)

        # remove the configuration
        disk.RemoveConfigurationItem((upd_conf[0][0], upd_conf[0][1]),
                                     self.no_options, dbus_interface=self.iface_prefix + '.Block')
        time.sleep(5)

        conf = disk.GetSecretConfiguration(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertEqual(len(conf), 0)

    def test_configuration_crypttab(self):

        # this test will change /etc/crypttab, we might want to revert the changes when it finishes
        crypttab = self.read_file('/etc/crypttab')
        self.addCleanup(self.write_file, '/etc/crypttab', crypttab)

        # format the disk
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        disk.Format('xfs', {'encrypt.passphrase': 'test'}, dbus_interface=self.iface_prefix + '.Block')

        # cleanup -- close the luks and remove format
        self.addCleanup(self._clean_format, disk)
        self.addCleanup(self._close_luks, disk)

        # configuration items as arrays of dbus.Byte
        opts = dbus.Array([dbus.Byte(ord(c)) for c in 'verify\0'],
                          signature=dbus.Signature('y'), variant_level=1)
        passwd = dbus.Array([dbus.Byte(ord(c)) for c in 'test\0'],
                            signature=dbus.Signature('y'), variant_level=1)

        # set the new configuration
        conf = dbus.Dictionary({'passphrase-contents': passwd,
                                'options': opts}, signature=dbus.Signature('sv'))
        disk.AddConfigurationItem(('crypttab', conf), self.no_options, dbus_interface=self.iface_prefix + '.Block')
        time.sleep(5)

        # get the configuration
        old_conf = self.get_property(disk, '.Block', 'Configuration')
        self.assertIsNotNone(old_conf)
        self.assertEqual(old_conf[0][1]['options'], opts)

        # get the secret configuration (passphrase)
        sec_conf = disk.GetSecretConfiguration(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertIsNotNone(sec_conf)
        self.assertEqual(sec_conf[0][1]['passphrase-contents'], passwd)

        # update the configuration
        new_opts = dbus.Array([dbus.Byte(ord(c)) for c in 'verify,discard\0'],
                              signature=dbus.Signature('y'), variant_level=1)
        new_conf = copy.deepcopy(sec_conf)
        new_conf[0][1]['options'] = new_opts

        disk.UpdateConfigurationItem((sec_conf[0][0], sec_conf[0][1]), (new_conf[0][0], new_conf[0][1]),
                                     self.no_options, dbus_interface=self.iface_prefix + '.Block')
        time.sleep(5)

        # get the configuration after the update
        upd_conf = self.get_property(disk, '.Block', 'Configuration')
        self.assertIsNotNone(upd_conf)
        self.assertEqual(upd_conf[0][1]['options'], new_opts)

        # remove the configuration
        disk.RemoveConfigurationItem((upd_conf[0][0], upd_conf[0][1]),
                                     self.no_options, dbus_interface=self.iface_prefix + '.Block')
        time.sleep(5)

        conf = disk.GetSecretConfiguration(self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.assertEqual(len(conf), 0)

    def test_rescan(self):

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        disk.Rescan(self.no_options, dbus_interface=self.iface_prefix + '.Block')
