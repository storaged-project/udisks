import udiskstestcase

import dbus
import glob
import os
import re
import six
import time
import unittest


class UdisksISCSITest(udiskstestcase.UdisksTestCase):
    '''Basic iSCSI test suite'''

    initiator = 'iqn.1994-05.com.redhat:iscsi-test'
    password = 'udisks'
    mutual_password = 'udisks-mutual'

    address = '127.0.0.1'
    port = 3260

    noauth_iqn = 'iqn.2003-01.udisks.test:iscsi-test-noauth'
    chap_iqn = 'iqn.2003-01.udisks.test:iscsi-test-chap'
    mutual_iqn = 'iqn.2003-01.udisks.test:iscsi-test-mutual'

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('ISCSI.Initiator'):
            raise unittest.SkipTest('Udisks module for iscsi tests not loaded, skipping.')

    def _force_lougout(self, target):
        self.run_command('iscsiadm --mode node --targetname %s --portal %s:%d '
                         '--logout' % (target, self.address, self.port))

    def _set_initiator_name(self):
        manager = self.get_object('/Manager')

        manager.SetInitiatorName(self.initiator, self.no_options,
                                 dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        init = manager.GetInitiatorName(self.no_options,
                                        dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')
        self.assertEqual(init, self.initiator)

    def test_login_noauth(self):
        manager = self.get_object('/Manager')
        nodes, _ = manager.DiscoverSendTargets(self.address, self.port, self.no_options,
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        node = next((node for node in nodes if node[0] == self.noauth_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node
        self.assertEqual(iqn, self.noauth_iqn)
        self.assertEqual(host, self.address)
        self.assertEqual(port, self.port)

        self.addCleanup(self._force_lougout, self.noauth_iqn)
        manager.Login(iqn, tpg, host, port, iface, self.no_options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertEqual(len(devs), 1)

        # check if the block device have 'Symlinks' property updated
        disk_name = os.path.realpath(devs[0]).split('/')[-1]
        disk_obj = self.get_object('/block_devices/' + disk_name)
        dbus_path = str(disk_obj.object_path)
        self.assertIsNotNone(disk_obj)

        symlinks = self.get_property_raw(disk_obj, '.Block', 'Symlinks')
        self.assertIn(self.str_to_ay(devs[0]), symlinks)

        manager.Logout(iqn, tpg, host, port, iface, self.no_options,
                       dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertEqual(len(devs), 0)

        # make sure the disk is no longer on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(dbus_path, objects.keys())

    def test_login_chap_auth(self):
        self._set_initiator_name()  # set initiator name to the one set in targetcli config

        manager = self.get_object('/Manager')
        nodes, _ = manager.DiscoverSendTargets(self.address, self.port, self.no_options,
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        node = next((node for node in nodes if node[0] == self.chap_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node
        self.assertEqual(iqn, self.chap_iqn)
        self.assertEqual(host, self.address)
        self.assertEqual(port, self.port)

        options = dbus.Dictionary(signature='sv')
        options['username'] = self.initiator

        # wrong password
        msg = 'Login failed: initiator reported error'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            options['password'] = '12345'
            manager.Login(iqn, tpg, host, port, iface, options,
                          dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        # right password
        options['password'] = self.password

        self.addCleanup(self._force_lougout, self.chap_iqn)
        manager.Login(iqn, tpg, host, port, iface, options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertEqual(len(devs), 1)

        # check if the block device have 'Symlinks' property updated
        disk_name = os.path.realpath(devs[0]).split('/')[-1]
        disk_obj = self.get_object('/block_devices/' + disk_name)
        dbus_path = str(disk_obj.object_path)
        self.assertIsNotNone(disk_obj)

        symlinks = self.get_property_raw(disk_obj, '.Block', 'Symlinks')
        self.assertIn(self.str_to_ay(devs[0]), symlinks)

        manager.Logout(iqn, tpg, host, port, iface, self.no_options,
                       dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertEqual(len(devs), 0)

        # make sure the disk is no longer on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(dbus_path, objects.keys())

    def test_login_mutual_auth(self):
        self._set_initiator_name()  # set initiator name to the one set in targetcli config

        manager = self.get_object('/Manager')
        nodes, _ = manager.DiscoverSendTargets(self.address, self.port, self.no_options,
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        node = next((node for node in nodes if node[0] == self.mutual_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node
        self.assertEqual(iqn, self.mutual_iqn)
        self.assertEqual(host, self.address)
        self.assertEqual(port, self.port)

        options = dbus.Dictionary(signature='sv')
        options['username'] = self.initiator
        options['password'] = self.password
        options['reverse-username'] = self.mutual_iqn
        options['reverse-password'] = self.mutual_password

        self.addCleanup(self._force_lougout, self.mutual_iqn)
        manager.Login(iqn, tpg, host, port, iface, options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertEqual(len(devs), 1)

        # check if the block device have 'Symlinks' property updated
        disk_name = os.path.realpath(devs[0]).split('/')[-1]
        disk_obj = self.get_object('/block_devices/' + disk_name)
        dbus_path = str(disk_obj.object_path)
        self.assertIsNotNone(disk_obj)

        symlinks = self.get_property_raw(disk_obj, '.Block', 'Symlinks')
        self.assertIn(self.str_to_ay(devs[0]), symlinks)

        manager.Logout(iqn, tpg, host, port, iface, self.no_options,
                       dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertEqual(len(devs), 0)

        # make sure the disk is no longer on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(dbus_path, objects.keys())

    def test_session(self):
        manager = self.get_object('/Manager')

        # first check if session objects are supported
        supported = self.get_property_raw(manager, '.Manager.ISCSI.Initiator', 'SessionsSupported')
        if not supported:
            udiskstestcase.UdisksTestCase.tearDownClass()
            self.skipTest("ISCSI.Session objects not supported.")

        nodes, _ = manager.DiscoverSendTargets(self.address, self.port, self.no_options,
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        node = next((node for node in nodes if node[0] == self.noauth_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node

        self.addCleanup(self._force_lougout, self.noauth_iqn)
        manager.Login(iqn, tpg, host, port, iface, self.no_options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        # /org/freedesktop/UDisks2/iscsi/sessionX should be created
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')

        sessions = [obj for obj in objects if re.match(r'.*/iscsi/session[0-9]+$', obj)]
        self.assertEqual(len(sessions), 1)

        session_path = sessions[0]
        session = self.bus.get_object(self.iface_prefix, session_path)

        dbus_target = self.get_property(session, '.ISCSI.Session', 'target_name')
        dbus_target.assertEqual(self.noauth_iqn)

        dbus_port = self.get_property(session, '.ISCSI.Session', 'persistent_port')
        dbus_port.assertEqual(self.port)

        dbus_address = self.get_property(session, '.ISCSI.Session', 'persistent_address')
        dbus_address.assertEqual(self.address)

        # logout using session
        session.Logout(self.no_options,
                       dbus_interface=self.iface_prefix + '.ISCSI.Session')

        # make sure the session object is no longer on dbus
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(session_path, objects.keys())
