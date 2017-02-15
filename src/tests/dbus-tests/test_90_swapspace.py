#!/bin/python3

import os
import dbus
import udiskstestcase


class UdisksSwapSpaceTest(udiskstestcase.UdisksTestCase):
    """This is SwapSpace related functions unit test"""

    def get_device(self, dev_name):
        dev = self.get_object('/block_devices/' + os.path.basename(dev_name))
        return dev

    def get_swapspace_iface(self, device):
        iface = dbus.Interface(device, dbus_interface=self.iface_prefix + '.Swapspace')
        return iface

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
        code, _ = self.run_command('swapon %s' % self.dev)
        self.assertEqual(code, 0)
        code, result = self.run_command('swapon --show')
        self.assertEqual(code, 0)
        # trailing space added to prevent false positive checks (e.g. /dev/sda vs. /dev/sda1)
        self.assertIn('%s ' % self.dev, result)

    def test_20_start_stop(self):
        self.run_command('mkswap %s' % self.dev)
        iface = self.get_swapspace_iface(self.device)
        self.udev_settle()

        # Active property is now in undefined state
        iface.Start(self.no_options)
        active = self.get_property(iface, '.Swapspace', 'Active')
        active.assertTrue()  # swap should be active

        code, result = self.run_command('swapon --show')
        self.assertEqual(code, 0)

        # swapon should return device name
        # trailing space added to prevent false positive checks (e.g. /dev/sda vs. /dev/sda1)
        self.assertIn('%s ' % self.dev, result)

        iface.Stop(self.no_options)
        active = self.get_property(iface, '.Swapspace', 'Active')
        active.assertFalse()  # swap shouldn't be active
