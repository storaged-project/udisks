import unittest
import storagedtestcase
import dbus
import os

class StoragedBaseTest(storagedtestcase.StoragedTestCase):
    '''This is a base test suite'''

    def test_10_manager(self):
        '''Testing the manager object presence'''
        manager = self.get_object('', '/Manager')
        self.assertIsNotNone(manager)
        version = self.get_property(manager, '.Manager', 'Version')
        self.assertIsNotNone(version)
        manager.EnableModules(True, dbus_interface=self.iface_prefix + '.Manager')


    def test_20_device_presence(self):
        '''Test the debug devices are present on the bus'''
        for d in self.vdevs:
            dev_obj = self.get_object('', "/block_devices/%s" % os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            self.assertTrue(os.path.exists(d))

