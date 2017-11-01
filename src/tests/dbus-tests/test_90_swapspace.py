#!/bin/python3

import os
import dbus
import udiskstestcase
from udiskstestcase import unstable_test

class UdisksSwapSpaceTest(udiskstestcase.UdisksTestCase):
    """This is SwapSpace related functions unit test"""

    def get_device(self, dev_name):
        dev = self.get_object('/block_devices/' + os.path.basename(dev_name))
        return dev

    def setUp(self):
        # create new fake swap device
        self.assertTrue(len(self.vdevs) > 0)
        self.dev = self.vdevs[0]
        self.device = self.get_device(self.dev)

    def tearDown(self):
        self.run_command('swapoff %s' % self.dev)
        dic = dbus.Dictionary(signature='sv')
        dic['erase'] = True
        self.device.Format('empty', dic, dbus_interface=self.iface_prefix + '.Block')

    def test_10_create(self):
        # test creating of the swap device
        self.device.Format('swap', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        fstype = self.get_property(self.device, '.Block', 'IdType')
        fstype.assertEqual('swap')

        code, _ = self.run_command('swapon %s' % self.dev)
        self.assertEqual(code, 0)
        code, result = self.run_command('swapon --show')
        self.assertEqual(code, 0)
        # trailing space added to prevent false positive checks (e.g. /dev/sda vs. /dev/sda1)
        self.assertIn('%s ' % self.dev, result)

    def test_20_start_stop(self):
        self.device.Format('swap', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        fstype = self.get_property(self.device, '.Block', 'IdType')
        fstype.assertEqual('swap')

        # start the swap
        self.device.Start(self.no_options, dbus_interface=self.iface_prefix + '.Swapspace')
        active = self.get_property(self.device, '.Swapspace', 'Active')
        active.assertTrue()  # swap should be active

        code, result = self.run_command('swapon --show')
        self.assertEqual(code, 0)

        # swapon should return device name
        # trailing space added to prevent false positive checks (e.g. /dev/sda vs. /dev/sda1)
        self.assertIn('%s ' % self.dev, result)

        self.device.Stop(self.no_options, dbus_interface=self.iface_prefix + '.Swapspace')
        active = self.get_property(self.device, '.Swapspace', 'Active')
        active.assertFalse()  # swap shouldn't be active

    @unstable_test
    def test_30_set_label(self):
        self.device.Format('swap', self.no_options, dbus_interface=self.iface_prefix + '.Block')

        fstype = self.get_property(self.device, '.Block', 'IdType')
        fstype.assertEqual('swap')

        # set label for the swap device
        self.device.SetLabel('udisks_swap', self.no_options, dbus_interface=self.iface_prefix + '.Swapspace')
        label = self.get_property(self.device, '.Block', 'IdLabel')
        label.assertEqual('udisks_swap')

        _ret, out = self.run_command('lsblk -noLABEL %s' % self.dev)
        self.assertEqual('udisks_swap', out.strip())
