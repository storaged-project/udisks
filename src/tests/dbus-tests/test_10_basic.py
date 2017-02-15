import udiskstestcase
import dbus
import os

class UdisksBaseTest(udiskstestcase.UdisksTestCase):
    '''This is a base test suite'''

    udisks_modules = set(['Bcache', 'BTRFS', 'ISCSI.Initiator', 'LVM2', 'ZRAM'])

    def setUp(self):
        self.manager_obj = self.get_object('/Manager')

    def _get_modules(self):
        content = self.read_file('/etc/os-release')
        release = {key: value for (key, value) in [line.split('=') for line in content.split('\n') if line]}
        distro = release['ID'].replace('"', '')

        if distro in ('redhat', 'centos'):
            return self.udisks_modules - {'Bcache'}
        else:
            return self.udisks_modules

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

    def test_80_device_presence(self):
        '''Test the debug devices are present on the bus'''
        for d in self.vdevs:
            dev_obj = self.get_object("/block_devices/%s" % os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            self.assertTrue(os.path.exists(d))
