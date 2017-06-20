import os
import dbus
import unittest

import udiskstestcase


class UdisksLsmLocalTestCase(udiskstestcase.UdisksTestCase):
    """
    Test suit for org.freedesktop.UDisks2.Drive.LsmLocal interface.
    """
    _LED_CONTROL_METHOD_NAMES = ["TurnFaultLEDOn", "TurnFaultLEDOff",
                                 "TurnIdentLEDOn", "TurnIdentLEDOff"]

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('LSM'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for LSM tests not loaded, skipping.')

    def _get_dbus_drv_obj(self, dbus_blk_obj):
        obj_path = self.get_property_raw(dbus_blk_obj, '.Block', 'Drive')
        return self.bus.get_object(self.iface_prefix, obj_path)

    def test_led_ctrl(self):
        """
        Tests:
         * Check the existence of LED control dbus methods.
         * Expecting to get a "org.freedesktop.UDisks2.Error.NotSupported"
           as we are running on LIO iscsi disks.
        """
        dbus_blk_obj = self.get_object('/block_devices/' +
                                       os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(dbus_blk_obj)
        dbus_drv_obj = self._get_dbus_drv_obj(dbus_blk_obj)
        self.assertIsNotNone(dbus_drv_obj)
        for method_name in UdisksLsmLocalTestCase._LED_CONTROL_METHOD_NAMES:
            method = dbus_drv_obj.get_dbus_method(
                method_name,
                dbus_interface=self.iface_prefix + ".Drive.LsmLocal")
            try:
                method(self.no_options)
                self.fail("LED control should failed as NotSupported on "
                          "LIO disks")
            except dbus.exceptions.DBusException as dbus_err:
                if dbus_err.get_dbus_name() != \
                   "org.freedesktop.UDisks2.Error.NotSupported":
                    raise
