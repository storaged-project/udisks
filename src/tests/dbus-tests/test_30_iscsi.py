import udiskstestcase

import dbus
import glob
import os
import re
import time
import shutil
import unittest


INITIATOR_FILE = "/etc/iscsi/initiatorname.iscsi"


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


    # Define common D-Bus method call timeout that needs to be slightly longer
    # than the corresponding timeout defined in libiscsi:
    #   #define ISCSID_REQ_TIMEOUT 1000
    # In reality the timeout is typically around 120 sec for the 'login' operation.
    iscsi_timeout = 1000 + 5

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('iscsi'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for iscsi tests not loaded, skipping.')

    def _force_lougout(self, target):
        self.run_command('iscsiadm --mode node --targetname %s --portal %s:%d '
                         '--logout' % (target, self.address, self.port))

    def _set_initiator_name(self):
        manager = self.get_object('/Manager')

        # make backup of INITIATOR_FILE and restore it at the end
        try:
            initiatorname_backup = self.read_file(INITIATOR_FILE)
            self.addCleanup(self.write_file, INITIATOR_FILE, initiatorname_backup)
        except FileNotFoundError as e:
            # no existing file, simply remove it once finished
            self.addCleanup(self.remove_file, INITIATOR_FILE, True)

        manager.SetInitiatorName(self.initiator, self.no_options,
                                 dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')

        # running iscsid needs to be restarted to reflect the change
        self.run_command('systemctl try-reload-or-restart iscsid.service')
        # ignore the return code in case of non-systemd distros

        init = manager.GetInitiatorName(self.no_options,
                                        dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')
        self.assertEqual(init, self.initiator)

    def _read_initator_name(self):
        with open(INITIATOR_FILE, "rb") as f:
            data = f.read()

        # in Python 2 data is string even when opening the file as 'rb'
        initiator = bytearray(data)
        return initiator.strip().split(b"InitiatorName=")[1]

    def _clean_iscsid_node_dir(self):
        for iqn in [self.noauth_iqn, self.chap_iqn, self.mutual_iqn]:
            shutil.rmtree(os.path.join('/var/lib/iscsi/nodes/', iqn), ignore_errors=True)

    def test__manager_interface(self):
        '''Test for module D-Bus Manager interface presence'''

        manager = self.get_object('/Manager')
        intro_data = manager.Introspect(self.no_options, dbus_interface='org.freedesktop.DBus.Introspectable')
        self.assertIn('interface name="%s.Manager.ISCSI.Initiator"' % self.iface_prefix, intro_data)

    def test_initiator_name(self):
        manager = self.get_object('/Manager')

        initiator_dbus = manager.GetInitiatorName(self.no_options,
                                                  dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')
        initiator_sys = self._read_initator_name()
        self.assertEqual(initiator_dbus, initiator_sys.decode())

        initiator_dbus = manager.GetInitiatorNameRaw(self.no_options,
                                                     dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator')
        # raw name is null terminated, we need to cut the last item from the bytearray
        self.assertEqual(bytearray(initiator_dbus)[:-1], initiator_sys)

    def test_login_noauth(self):
        manager = self.get_object('/Manager')
        nodes, _ = manager.DiscoverSendTargets(self.address, self.port, self.no_options,
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                                               timeout=self.iscsi_timeout)
        self.addCleanup(self._clean_iscsid_node_dir)

        node = next((node for node in nodes if node[0] == self.noauth_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node
        self.assertEqual(iqn, self.noauth_iqn)
        self.assertEqual(host, self.address)
        self.assertEqual(port, self.port)

        self.addCleanup(self._force_lougout, self.noauth_iqn)
        manager.Login(iqn, tpg, host, port, iface, self.no_options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                      timeout=self.iscsi_timeout)

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
                       dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                       timeout=self.iscsi_timeout)

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
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                                               timeout=self.iscsi_timeout)
        self.addCleanup(self._clean_iscsid_node_dir)

        node = next((node for node in nodes if node[0] == self.chap_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node
        self.assertEqual(iqn, self.chap_iqn)
        self.assertEqual(host, self.address)
        self.assertEqual(port, self.port)

        options = dbus.Dictionary(signature='sv')
        options['node.session.auth.chap_algs'] = 'SHA3-256,SHA256,SHA1'  # disallow MD5
        options['username'] = self.initiator

        msg = r'Login failed: initiator reported error \(24 - iSCSI login failed due to authorization failure\)'
        # missing auth info
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            manager.Login(iqn, tpg, host, port, iface, self.no_options,
                          dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                          timeout=self.iscsi_timeout)

        # wrong password
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            options['password'] = '12345'
            manager.Login(iqn, tpg, host, port, iface, options,
                          dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                          timeout=self.iscsi_timeout)

        # right password
        options['password'] = self.password

        self.addCleanup(self._force_lougout, self.chap_iqn)
        manager.Login(iqn, tpg, host, port, iface, options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                      timeout=self.iscsi_timeout)

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
                       dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                       timeout=self.iscsi_timeout)

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
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                                               timeout=self.iscsi_timeout)
        self.addCleanup(self._clean_iscsid_node_dir)

        node = next((node for node in nodes if node[0] == self.mutual_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node
        self.assertEqual(iqn, self.mutual_iqn)
        self.assertEqual(host, self.address)
        self.assertEqual(port, self.port)

        options = dbus.Dictionary(signature='sv')
        options['node.session.auth.chap_algs'] = 'SHA3-256,SHA256,SHA1'  # disallow MD5
        options['username'] = self.initiator
        options['password'] = self.password
        options['reverse-username'] = self.mutual_iqn
        options['reverse-password'] = self.mutual_password

        self.addCleanup(self._force_lougout, self.mutual_iqn)
        manager.Login(iqn, tpg, host, port, iface, options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                      timeout=self.iscsi_timeout)

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
                       dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                       timeout=self.iscsi_timeout)

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
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                                               timeout=self.iscsi_timeout)
        self.addCleanup(self._clean_iscsid_node_dir)

        node = next((node for node in nodes if node[0] == self.noauth_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node

        self.addCleanup(self._force_lougout, self.noauth_iqn)
        manager.Login(iqn, tpg, host, port, iface, self.no_options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                      timeout=self.iscsi_timeout)

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
                       dbus_interface=self.iface_prefix + '.ISCSI.Session',
                       timeout=self.iscsi_timeout)

        # make sure the session object is no longer on dbus
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(session_path, objects.keys())

    def test_login_noauth_badauth(self):
        """
        Test auth info override
        """
        manager = self.get_object('/Manager')
        nodes, _ = manager.DiscoverSendTargets(self.address, self.port, self.no_options,
                                               dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                                               timeout=self.iscsi_timeout)
        self.addCleanup(self._clean_iscsid_node_dir)

        node = next((node for node in nodes if node[0] == self.noauth_iqn), None)
        self.assertIsNotNone(node)

        (iqn, tpg, host, port, iface) = node
        self.assertEqual(iqn, self.noauth_iqn)
        self.assertEqual(host, self.address)
        self.assertEqual(port, self.port)

        self.addCleanup(self._force_lougout, self.noauth_iqn)

        # first attempt - wrong password
        options = dbus.Dictionary(signature='sv')
        options['node.session.auth.chap_algs'] = 'SHA3-256,SHA256,SHA1'  # disallow MD5
        options['username'] = self.initiator
        msg = r'Login failed: initiator reported error \((19 - encountered non-retryable iSCSI login failure|24 - iSCSI login failed due to authorization failure)\)'
        with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
            options['password'] = '12345'
            manager.Login(iqn, tpg, host, port, iface, options,
                          dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                          timeout=self.iscsi_timeout)

        # second atttempt - no password
        manager.Login(iqn, tpg, host, port, iface, self.no_options,
                      dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                      timeout=self.iscsi_timeout)

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
                       dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                       timeout=self.iscsi_timeout)

        devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
        self.assertEqual(len(devs), 0)

        # make sure the disk is no longer on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(dbus_path, objects.keys())

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_ibft(self):
        """
        Test iBFT discovery and login if available
        """
        if not os.path.exists('/sys/firmware/acpi/tables/iBFT'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            self.skipTest('No iBFT ACPI table detected')
        ret, out = udiskstestcase.run_command('modprobe iscsi_ibft')
        if ret != 0:
            udiskstestcase.UdisksTestCase.tearDownClass()
            self.skipTest('iscsi_ibft kernel module unavailable')

        manager = self.get_object('/Manager')
        nodes, _ = manager.DiscoverFirmware(self.no_options,
                                            dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                                            timeout=self.iscsi_timeout)
        self.addCleanup(self._clean_iscsid_node_dir)

        # let's try to connect in whatever was discovered
        for node in nodes:
            (iqn, tpg, host, port, iface) = node
            self.assertIsNotNone(iqn)
            self.assertIsNotNone(host)
            self.assertIsNotNone(port)

            self.addCleanup(self._force_lougout, iqn)

            # no password
            options = dbus.Dictionary(signature='sv')
            options['node.session.auth.chap_algs'] = 'SHA3-256,SHA256,SHA1'  # disallow MD5
            manager.Login(iqn, tpg, host, port, iface, options,
                          dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                          timeout=self.iscsi_timeout)

            devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
            self.assertGreater(len(devs), 0)

            manager.Logout(iqn, tpg, host, port, iface, self.no_options,
                           dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                           timeout=self.iscsi_timeout)

            devs = glob.glob('/dev/disk/by-path/*%s*' % iqn)
            self.assertEqual(len(devs), 0)

            # second attempt - wrong password
            msg = r'Login failed: initiator reported error \((19 - encountered non-retryable iSCSI login failure|24 - iSCSI login failed due to authorization failure)\)'
            with self.assertRaisesRegex(dbus.exceptions.DBusException, msg):
                options['username'] = 'nonsenseuser'
                options['password'] = '12345'
                manager.Login(iqn, tpg, host, port, iface, options,
                              dbus_interface=self.iface_prefix + '.Manager.ISCSI.Initiator',
                              timeout=self.iscsi_timeout)
