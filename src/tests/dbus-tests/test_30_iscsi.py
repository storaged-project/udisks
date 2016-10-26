import unittest
import storagedtestcase
import os
import glob
import time

class StoragedISCSITest(storagedtestcase.StoragedTestCase):
    '''Basic iSCSI test suite'''

    @unittest.skipUnless('TEST_ISCSI_HOST' in os.environ, "$TEST_ISCSI_HOST not set")
    def test_login_noauth(self):
        host = os.getenv('TEST_ISCSI_HOST')
        self.assertIsNotNone(host)
        manager = self.get_object('', '/Manager')
        (nodes, nodes_num) = manager.DiscoverSendTargets(host, 0, self.no_options,
                dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')
        self.assertGreater(nodes_num, 0)
        (iqn, tpg, host, port, iface) = nodes[0]
        manager.Login(iqn, tpg, host, port, iface, self.no_options,
                dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')
        time.sleep(1)
        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertGreater(len(devs), 0)
        manager.Logout(iqn, tpg, host, port, iface, self.no_options,
                dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')
        time.sleep(1)
        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertEqual(len(devs), 0)


