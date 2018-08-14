import os
import re
import time
import six
import dbus
import unittest

import gi
gi.require_version('BlockDev', '2.0')
from gi.repository import BlockDev

from distutils.spawn import find_executable

import udiskstestcase


VDO_CONFIG = '/etc/vdoconf.yml'


class UdisksVDOTest(udiskstestcase.UdisksTestCase):
    '''This is a basic VDO test suite'''

    LOOP_DEVICE_PATH = '/var/tmp/udisks_test_disk_vdo'

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('VDO'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for VDO tests not loaded, skipping.')

        if not find_executable('vdo'):
            raise unittest.SkipTest('vdo executable not foundin $PATH, skipping.')

        if not BlockDev.utils_have_kernel_module('kvdo'):
            raise unittest.SkipTest('VDO kernel module not available, skipping.')

    def setUp(self):
        # create backing sparse file
        # VDO needs at least 5G of space and we need some room for the grow test
        # ...rumors go that vdo internally operates on 2G extents...
        self.run_command('truncate -s 8G %s' % self.LOOP_DEVICE_PATH)
        ret_code, self.dev_name = self.run_command('losetup --find --show %s' % self.LOOP_DEVICE_PATH)
        self.assertEqual(ret_code, 0)
        time.sleep(0.5)
        self.device = self.get_device(self.dev_name)
        self.assertIsNotNone(self.device)
        super(UdisksVDOTest, self).setUp()

        # revert any changes in the /etc/vdoconf.yml
        if os.path.exists(VDO_CONFIG):
            vdo_config = self.read_file(VDO_CONFIG)
            self.addCleanup(self.write_file, VDO_CONFIG, vdo_config)
        else:
            self.addCleanup(self.remove_file, VDO_CONFIG, True)

    def tearDown(self):
        # tear down loop device
        self.run_command('losetup --detach %s' % self.dev_name)
        os.remove(self.LOOP_DEVICE_PATH)
        super(UdisksVDOTest, self).tearDown()

    def _force_remove(self, vdo_name):
         if os.path.exists('/dev/mapper/%s' % vdo_name):
             ret, _out = self.run_command('vdo stop --force --name %s' % vdo_name)
             if ret != 0:
                 self.fail('Failed to stop the vdo volume %s' % vdo_name)
             ret, _out = self.run_command('vdo remove --force --name %s' % vdo_name)
             if ret != 0:
                 self.fail('Failed to remove vdo volume %s' % vdo_name)

    def test_create_and_attributes(self):
        '''Test creating a new vdo volume and verify its properties'''

        vdo_name = 'udisks_basic_test'

        manager = self.get_object('/Manager')
        vdo_path = manager.CreateVolume(vdo_name,
                                        self.device.object_path,
                                        0,       # logical_size
                                        0,       # index_memory
                                        True,    # compression
                                        True,    # deduplication
                                        'auto',  # write_policy
                                        self.no_options,
                                        dbus_interface=self.iface_prefix + '.Manager.VDO')
        self.assertIsNotNone(vdo_path)

        vdo = self.get_object(vdo_path)
        self.addCleanup(self._force_remove, vdo_name)

        # check that the volume exists
        self.assertTrue(os.path.exists('/dev/mapper/%s' % vdo_name))
        ret_code, _out = self.run_command('dmsetup info %s' % vdo_name)
        self.assertEqual(ret_code, 0)

        # check attribute setting and value refresh
        prop_compression = self.get_property(vdo, '.Block.VDO', 'Compression')
        prop_compression.assertEqual(True)
        vdo.EnableCompression(True, self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        prop_compression = self.get_property(vdo, '.Block.VDO', 'Compression')
        prop_compression.assertEqual(True)
        vdo.EnableCompression(False, self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        prop_compression = self.get_property(vdo, '.Block.VDO', 'Compression')
        prop_compression.assertEqual(False)

        prop_deduplication = self.get_property(vdo, '.Block.VDO', 'Deduplication')
        prop_deduplication.assertEqual(True)
        vdo.EnableDeduplication(False, self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        prop_deduplication = self.get_property(vdo, '.Block.VDO', 'Deduplication')
        prop_deduplication.assertEqual(False)
        ret, _out = self.run_command('vdo enableDeduplication --name %s' % vdo_name)
        self.assertEqual(ret, 0)
        prop_deduplication = self.get_property(vdo, '.Block.VDO', 'Deduplication')
        # github/issues/541: this should be True however the `vdo` call doesn't generate udev event on attribute change
        prop_deduplication.assertEqual(False)

        prop_active = self.get_property(vdo, '.Block.VDO', 'Active')
        prop_active.assertEqual(True)

        prop_write_policy = self.get_property(vdo, '.Block.VDO', 'WritePolicy')
        prop_write_policy.assertEqual('auto')
        vdo.ChangeWritePolicy('sync', self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        prop_write_policy = self.get_property(vdo, '.Block.VDO', 'WritePolicy')
        prop_write_policy.assertEqual('sync')
        vdo.ChangeWritePolicy('async', self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        prop_write_policy = self.get_property(vdo, '.Block.VDO', 'WritePolicy')
        prop_write_policy.assertEqual('async')
        vdo.ChangeWritePolicy('auto', self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        prop_write_policy = self.get_property(vdo, '.Block.VDO', 'WritePolicy')
        prop_write_policy.assertEqual('auto')

        prop_logical_size = self.get_property(vdo, '.Block.VDO', 'LogicalSize')
        prop_logical_size.assertGreater(0)
        vdo.GrowLogical(prop_logical_size.value + 8192, self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        new_logical_size = self.get_property(vdo, '.Block.VDO', 'LogicalSize')
        new_logical_size.assertEqual(prop_logical_size.value + 8192)

        prop_index_memory = self.get_property(vdo, '.Block.VDO', 'IndexMemory')
        prop_index_memory.assertGreater(0)

        # test getting statistics
        stats = vdo.GetStatistics(self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        self.assertIsNotNone(stats)
        self.assertGreater(len(stats), 0)
        self.assertIn("block_size", stats)

        # destroy the volume
        vdo.Remove(False, self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')

        # make sure the vdo volume is not on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(vdo_path, objects.keys())

        self.assertFalse(os.path.exists('/dev/mapper/%s' % vdo_name))

    def test_activate_deactivate(self):
        '''Test start/stop and activate/deactivate'''

        vdo_name = 'udisks_activate_test'

        manager = self.get_object('/Manager')

        # first test a failure during creation
        msg = 'org.freedesktop.UDisks2.Error.Failed: Invalid object path.*'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            vdo_path = manager.CreateVolume(vdo_name,
                                            '/org/freedesktop/UDisks2/block_devices/nonsensetest1234566777',
                                            0,       # logical_size
                                            0,       # index_memory
                                            True,    # compression
                                            False,   # deduplication
                                            'sync',  # write_policy
                                            self.no_options,
                                            dbus_interface=self.iface_prefix + '.Manager.VDO')

        # create a real volume to work on
        vdo_path = manager.CreateVolume(vdo_name,
                                        self.device.object_path,
                                        0,       # logical_size
                                        0,       # index_memory
                                        True,    # compression
                                        False,   # deduplication
                                        'sync',  # write_policy
                                        self.no_options,
                                        dbus_interface=self.iface_prefix + '.Manager.VDO')
        self.assertIsNotNone(vdo_path)

        vdo = self.get_object(vdo_path)
        self.addCleanup(self._force_remove, vdo_name)

        # deactivate the volume
        prop_active = self.get_property(vdo, '.Block.VDO', 'Active')
        prop_active.assertEqual(True)
        vdo.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        prop_active = self.get_property(vdo, '.Block.VDO', 'Active')
        prop_active.assertEqual(False)

        # stop the volume
        vdo.Stop(False, self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        # make sure the vdo volume is not on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(vdo_path, objects.keys())
        # make sure it's not in DM either
        self.assertFalse(os.path.exists('/dev/mapper/%s' % vdo_name))

        # attempt to start non-existing volume
        msg = 'VDO volume .* not found'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            vdo_path = manager.StartVolumeByName('nonsense123345', False, self.no_options, dbus_interface=self.iface_prefix + '.Manager.VDO')

        # attempt to start deactivated volume
        # XXX - bug in older vdo: returns 0 even when it cannot be started, catching the consequences here
        msg = 'org.freedesktop.UDisks2.Error.Failed: (Error waiting .*: Timed out waiting for object|Error starting volume: Process reported exit code 5: .* not activated)'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            vdo_path = manager.StartVolumeByName(vdo_name, False, self.no_options, dbus_interface=self.iface_prefix + '.Manager.VDO')

        # attempt to activate non-existing volume
        msg = 'VDO volume .* not found'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            vdo_path = manager.ActivateVolumeByName('nonsense123999', self.no_options, dbus_interface=self.iface_prefix + '.Manager.VDO')

        # activate the working volume and start it again
        manager.ActivateVolumeByName(vdo_name, self.no_options, dbus_interface=self.iface_prefix + '.Manager.VDO')
        vdo_path = manager.StartVolumeByName(vdo_name, False, self.no_options, dbus_interface=self.iface_prefix + '.Manager.VDO')

        # check that the volume exists
        self.assertTrue(os.path.exists('/dev/mapper/%s' % vdo_name))
        ret, _out = self.run_command('dmsetup info %s' % vdo_name)
        self.assertEqual(ret, 0)

        # check attributes passed in during creation
        prop_compression = self.get_property(vdo, '.Block.VDO', 'Compression')
        prop_compression.assertEqual(True)
        prop_deduplication = self.get_property(vdo, '.Block.VDO', 'Deduplication')
        prop_deduplication.assertEqual(False)
        prop_write_policy = self.get_property(vdo, '.Block.VDO', 'WritePolicy')
        prop_write_policy.assertEqual('sync')

        # destroy the volume
        vdo.Remove(False, self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        self.assertFalse(os.path.exists('/dev/mapper/%s' % vdo_name))

    def test_grow_physical(self):
        '''Test org.freedesktop.UDisks2.Block.VDO.GrowPhysical()'''

        vdo_name = 'udisks_grow_physical'

        # create a partition that we can grow later
        ret, _out = self.run_command('parted %s mklabel gpt' % self.dev_name)
        self.assertEqual(ret, 0)
        ret, _out = self.run_command('parted %s mkpart primary 1M 5.1G' % self.dev_name)
        self.assertEqual(ret, 0)

        vdo_part_name = self.dev_name + 'p1'
        time.sleep(0.5)
        self.assertTrue(os.path.exists(vdo_part_name))
        vdo_part_dev = self.get_device(vdo_part_name)
        self.assertIsNotNone(vdo_part_dev)

        manager = self.get_object('/Manager')

        vdo_path = manager.CreateVolume(vdo_name,
                                        vdo_part_dev.object_path,
                                        0,       # logical_size
                                        0,       # index_memory
                                        True,    # compression
                                        True,    # deduplication
                                        'auto',  # write_policy
                                        self.no_options,
                                        dbus_interface=self.iface_prefix + '.Manager.VDO')
        self.assertIsNotNone(vdo_path)
        vdo = self.get_object(vdo_path)
        self.addCleanup(self._force_remove, vdo_name)

        orig_logical_size = self.get_property(vdo, '.Block.VDO', 'LogicalSize')
        orig_logical_size.assertGreater(0)
        orig_physical_size = self.get_property(vdo, '.Block.VDO', 'PhysicalSize')
        orig_physical_size.assertGreater(0)

        # no room to grow, expect a failure and no change in property value
        msg = '(Cannot grow physical on VDO|Cannot prepare to grow physical on VDO .*; device-mapper: message ioctl on .* failed: Invalid argument)'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            vdo.GrowPhysical(self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        new_physical_size = self.get_property(vdo, '.Block.VDO', 'PhysicalSize')
        new_physical_size.assertEqual(orig_physical_size.value)

        # grow the partition
        # may seem a bit dangerous since we're operating on a live volume but actually
        # vdo somehow inherits new physical size on start, so we really need to grow on a live volume
        ret, _out = self.run_command('parted %s resizepart 1 8G' % self.dev_name)
        self.assertEqual(ret, 0)
        time.sleep(0.5)
        new_physical_size = self.get_property(vdo, '.Block.VDO', 'PhysicalSize')
        new_physical_size.assertEqual(orig_physical_size.value)

        # perform the real grow and get new sizes
        vdo.GrowPhysical(self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        new_logical_size = self.get_property(vdo, '.Block.VDO', 'LogicalSize')
        new_logical_size.assertGreater(0)
        new_physical_size = self.get_property(vdo, '.Block.VDO', 'PhysicalSize')
        new_physical_size.assertGreater(orig_physical_size.value)

        # destroy the volume
        vdo.Remove(False, self.no_options, dbus_interface=self.iface_prefix + '.Block.VDO')
        self.assertFalse(os.path.exists('/dev/mapper/%s' % vdo_name))
