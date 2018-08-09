import udiskstestcase
import dbus
import os
from distutils.spawn import find_executable

class UdisksBaseTest(udiskstestcase.UdisksTestCase):
    '''This is a base test suite'''

    udisks_modules = set(['Bcache', 'BTRFS', 'ISCSI.Initiator', 'LVM2', 'ZRAM', 'VDO'])

    def setUp(self):
        self.manager_obj = self.get_object('/Manager')

    def _get_modules(self):
        distro, version = self.distro
        modules = self.udisks_modules
        if distro in ('enterprise_linux', 'centos') and version == '7':
            modules = modules - {'Bcache'}
        elif distro in ('enterprise_linux', 'centos') and int(version) > 7:
            modules = modules - {'Bcache', 'BTRFS'}
        # assuming the kvdo module is typically pulled in as a vdo tool dependency
        if not find_executable("vdo"):
            modules = modules - {'VDO'}
        return modules

    def test_10_manager(self):
        '''Testing the manager object presence'''
        self.assertIsNotNone(self.manager_obj)
        version = self.get_property(self.manager_obj, '.Manager', 'Version')
        version.assertIsNotNone()

    def test_20_enable_modules(self):
        manager = self.get_interface(self.manager_obj, '.Manager')
        manager_intro = dbus.Interface(self.manager_obj, "org.freedesktop.DBus.Introspectable")
        intro_data = manager_intro.Introspect()
        modules = self._get_modules()
        modules_loaded = any('interface name="%s.Manager.%s"' % (self.iface_prefix, module)
                             in intro_data for module in modules)

        if modules_loaded:
            self.skipTest("Modules already loaded, nothing to test")
        else:
            manager.EnableModules(dbus.Boolean(True))
            intro_data = manager_intro.Introspect()

            for module in modules:
                self.assertIn('interface name="%s.Manager.%s"' % (self.iface_prefix, module), intro_data)

    def test_30_supported_filesystems(self):
        fss = self.get_property(self.manager_obj, '.Manager', 'SupportedFilesystems')
        self.assertEqual({str(s) for s in fss.value},
                         {'nilfs2', 'btrfs', 'swap', 'ext3', 'udf', 'xfs', 'minix', 'ext2', 'ext4', 'f2fs', 'reiserfs', 'ntfs', 'vfat', 'exfat'})

    def test_40_can_format(self):
        '''Test for installed filesystem creation utility with CanFormat'''
        manager = self.get_interface(self.manager_obj, '.Manager')
        with self.assertRaises(dbus.exceptions.DBusException):
            manager.CanFormat('wxyz')
        avail, util = manager.CanFormat('xfs')
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'mkfs.xfs')
        self.assertEqual(avail, find_executable('mkfs.xfs') is not None)
        avail, util = manager.CanFormat('f2fs')
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'mkfs.f2fs')
        self.assertEqual(avail, find_executable('mkfs.f2fs') is not None)
        avail, util = manager.CanFormat('ext4')
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'mkfs.ext4')
        self.assertEqual(avail, find_executable('mkfs.ext4') is not None)
        for fs in map(str, self.get_property(self.manager_obj, '.Manager', 'SupportedFilesystems').value):
            avail, util = manager.CanFormat(fs)
            # currently UDisks relies on executables for filesystem creation
            if avail:
                self.assertEqual(util, '')
            else:
                self.assertGreater(len(util), 0)

    def test_40_can_resize(self):
        '''Test for installed filesystem resize utility with CanResize'''
        offline_shrink = 0b00010
        offline_grow   = 0b00100
        online_shrink  = 0b01000
        online_grow    = 0b10000
        manager = self.get_interface(self.manager_obj, '.Manager')
        with self.assertRaises(dbus.exceptions.DBusException):
            manager.CanResize('nilfs2')
        avail, mode, util = manager.CanResize('xfs')
        # the resize mode flage values are defined in the method documentation
        self.assertEqual(mode, online_grow | offline_grow)
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'xfs_growfs')
        self.assertEqual(avail, find_executable('xfs_growfs') is not None)
        avail, mode, util = manager.CanResize('ext4')
        self.assertEqual(mode, offline_shrink | offline_grow | online_grow)
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'resize2fs')
        self.assertEqual(avail, find_executable('resize2fs') is not None)
        avail, mode, util = manager.CanResize('vfat')
        self.assertTrue(avail)  # libparted, no executable
        self.assertEqual(util, '')
        self.assertEqual(mode, offline_shrink | offline_grow)

    def test_40_can_repair(self):
        '''Test for installed filesystem repair utility with CanRepair'''
        manager = self.get_interface(self.manager_obj, '.Manager')
        with self.assertRaises(dbus.exceptions.DBusException):
            manager.CanRepair('nilfs2')
        avail, util = manager.CanRepair('xfs')
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'xfs_repair')
        self.assertEqual(avail, find_executable('xfs_repair') is not None)
        avail, util = manager.CanRepair('ext4')
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'e2fsck')
        self.assertEqual(avail, find_executable('e2fsck') is not None)
        avail, util = manager.CanRepair('vfat')
        self.assertTrue(avail)  # libparted, no executable
        self.assertEqual(util, '')

    def test_40_can_check(self):
        '''Test for installed filesystem check utility with CanCheck'''
        manager = self.get_interface(self.manager_obj, '.Manager')
        with self.assertRaises(dbus.exceptions.DBusException):
            manager.CanCheck('nilfs2')
        avail, util = manager.CanCheck('xfs')
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'xfs_db')
        self.assertEqual(avail, find_executable('xfs_db') is not None)
        avail, util = manager.CanCheck('ext4')
        if avail:
            self.assertEqual(util, '')
        else:
            self.assertEqual(util, 'e2fsck')
        self.assertEqual(avail, find_executable('e2fsck') is not None)
        avail, util = manager.CanCheck('vfat')
        self.assertTrue(avail)  # libparted, no executable
        self.assertEqual(util, '')

    def test_50_get_block_devices(self):
        # get all objects and filter block devices
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        block_paths = [p for p in list(objects.keys()) if "/block_devices/" in p]

        # get block devices using the 'GetBlockDevices' function
        manager = self.get_interface(self.manager_obj, '.Manager')
        dbus_blocks = manager.GetBlockDevices(self.no_options)

        # and make sure both lists are equal
        self.assertEqual(len(block_paths), len(dbus_blocks))
        for path in block_paths:
            self.assertIn(path, dbus_blocks)

    def _wipe(self, device, retry=True):
        ret, out = self.run_command('wipefs -a %s' % device)
        if ret != 0:
            if retry:
                self._wipe(device, False)
            else:
                self.fail('Failed to wipe device %s: %s' % (device, out))

    def test_60_resolve_device(self):
        manager = self.get_interface(self.manager_obj, '.Manager')

        # try some non-existing device first
        spec = dbus.Dictionary({'path': '/dev/i-dont-exist'}, signature='sv')
        devices = manager.ResolveDevice(spec, self.no_options)
        self.assertEqual(len(devices), 0)

        # get our first virtual disk by path
        spec = dbus.Dictionary({'path': self.vdevs[0]}, signature='sv')
        devices = manager.ResolveDevice(spec, self.no_options)
        object_path = '%s/block_devices/%s' % (self.path_prefix, os.path.basename(self.vdevs[0]))

        self.assertEqual(len(devices), 1)
        self.assertIn(object_path, devices)

        # try to get it with some symlink path
        ldir = '/dev/disk/by-id'
        link = next(os.path.join(ldir, l) for l in os.listdir(ldir)
                    if os.path.realpath(os.path.join(ldir, l)) == self.vdevs[0])

        spec = dbus.Dictionary({'path': link}, signature='sv')
        devices = manager.ResolveDevice(spec, self.no_options)
        object_path = '%s/block_devices/%s' % (self.path_prefix, os.path.basename(self.vdevs[0]))

        self.assertEqual(len(devices), 1)
        self.assertIn(object_path, devices)

        # try to get the disk by specifying both path and label (it has no label
        # so this should return an empty list)
        spec = dbus.Dictionary({'path': self.vdevs[0], 'label': 'test'}, signature='sv')
        devices = manager.ResolveDevice(spec, self.no_options)

        self.assertEqual(len(devices), 0)

        # format the disk to ext4 with label
        label = 'test'
        ret, out = self.run_command('mkfs.ext4 -F -L %s %s' % (label, self.vdevs[0]))
        if ret != 0:
            self.fail('Failed to create ext4 filesystem on %s: %s' % (self.vdevs[0], out))
        self.addCleanup(self._wipe, self.vdevs[0])

        # we run mkfs manually, now just wait for the property to update
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        dbus_label = self.get_property(disk, '.Block', 'IdLabel')
        dbus_label.assertEqual(label)

        # now it should return the disk
        spec = dbus.Dictionary({'path': self.vdevs[0], 'label': label}, signature='sv')
        devices = manager.ResolveDevice(spec, self.no_options)
        object_path = '%s/block_devices/%s' % (self.path_prefix, os.path.basename(self.vdevs[0]))

        self.assertEqual(len(devices), 1)
        self.assertIn(object_path, devices)

        # format another disk to ext4 with the same label, ResolveDevice should
        # now return both devices
        ret, _out = self.run_command('mkfs.ext4 -F -L %s %s' % (label, self.vdevs[1]))
        if ret != 0:
            self.fail('Failed to create ext4 filesystem on %s: %s' % (self.vdevs[1], out))
        self.addCleanup(self._wipe, self.vdevs[1])

        # we run mkfs manually, now just wait for the property to update
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[1]))
        dbus_label = self.get_property(disk, '.Block', 'IdLabel')
        dbus_label.assertEqual(label)

        spec = dbus.Dictionary({'label': label}, signature='sv')
        devices = manager.ResolveDevice(spec, self.no_options)
        object_path1 = '%s/block_devices/%s' % (self.path_prefix, os.path.basename(self.vdevs[0]))
        object_path2 = '%s/block_devices/%s' % (self.path_prefix, os.path.basename(self.vdevs[1]))

        self.assertEqual(len(devices), 2)
        self.assertIn(object_path1, devices)
        self.assertIn(object_path2, devices)

        # and now just check that simple resolving using uuid works
        uuid = self.get_property_raw(disk, '.Block', 'IdUUID')
        spec = dbus.Dictionary({'uuid': uuid}, signature='sv')
        devices = manager.ResolveDevice(spec, self.no_options)
        object_path = '%s/block_devices/%s' % (self.path_prefix, os.path.basename(self.vdevs[1]))

        self.assertEqual(len(devices), 1)
        self.assertIn(object_path, devices)

    def test_80_device_presence(self):
        '''Test the debug devices are present on the bus'''
        for d in self.vdevs:
            dev_obj = self.get_object("/block_devices/%s" % os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            self.assertTrue(os.path.exists(d))
