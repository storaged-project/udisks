import storagedtestcase
import dbus
import os

class StoragedBaseTest(storagedtestcase.StoragedTestCase):
    '''This is a base test suite'''

    def setUp(self):
        self.manager_obj = self.get_object('/Manager')

    def test_10_manager(self):
        '''Testing the manager object presence'''
        self.assertIsNotNone(self.manager_obj)
        version = self.get_property(self.manager_obj, '.Manager', 'Version')
        version.assertIsNotNone()

    def test_20_enable_modules(self):
        manager = self.get_interface(self.manager_obj, '.Manager')
        manager_intro = dbus.Interface(self.manager_obj, "org.freedesktop.DBus.Introspectable")
        intro_data = manager_intro.Introspect()
        modules_loaded = 'interface name="org.freedesktop.UDisks2.Manager.Bcache"' in intro_data

        if modules_loaded:
            self.skipTest("Modules already loaded, nothing to test")
        else:
            manager.EnableModules(dbus.Boolean(True))
            intro_data = manager_intro.Introspect()
            self.assertIn('interface name="org.freedesktop.UDisks2.Manager.Bcache"', intro_data)

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
