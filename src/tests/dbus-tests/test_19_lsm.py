import os
import re
import six
import dbus
import unittest
import tempfile

from distutils.spawn import find_executable

import udiskstestcase

class UdisksLSMTestCase(udiskstestcase.UdisksTestCase):
    """
    Provide as much LSM module code coverage as possible using the libstoragemgmt 'sim' plugin.
    https://libstorage.github.io/libstoragemgmt-doc/
    """
    _LED_CONTROL_METHOD_NAMES = ["TurnFaultLEDOn", "TurnFaultLEDOff",
                                 "TurnIdentLEDOn", "TurnIdentLEDOff"]

    lsm_init_done = False
    lsm_db_file = None
    lsm_module_conf_contents = None

    @classmethod
    def _get_lsm_module_conf_path(cls):
        _CONF_FILE = 'modules.conf.d/udisks2_lsm.conf'
        if os.environ['UDISKS_TESTS_ARG_SYSTEM'] == '1':
            return os.path.join('/etc/udisks2/', _CONF_FILE)
        else:
            return os.path.join(os.environ['UDISKS_TESTS_PROJDIR'], 'udisks', _CONF_FILE)

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()

        # sqlite3 is needed to hack the lsm sim db
        if not find_executable('sqlite3'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('LSM: sqlite3 executable not found in $PATH, skipping.')

        # check that lsmd is running
        ret, _out = cls.run_command('pidof lsmd')
        if ret != 0:
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('libstoragemgmt lsmd daemon not running, skipping.')

        cls.lsm_module_conf_path = cls._get_lsm_module_conf_path()
        try:
            # back the original config file up if it exists
            cls.lsm_module_conf_contents = cls.read_file(cls.lsm_module_conf_path)
        except IOError:
            # python2 error
            pass
        except FileNotFoundError:
            # no existing udisks2_lsm.conf, simply remove the file once finished
            pass

        # try loading the module with no config file
        cls.remove_file(cls.lsm_module_conf_path, True)
        try:
            # module load should not succeed with no config file
            if not cls.check_module_loaded('lsm'):
                # no exception raised, module is not installed
                cls.tearDownClass()
                udiskstestcase.UdisksTestCase.tearDownClass()
                raise unittest.SkipTest('UDisks LSM module not available, skipping.')
            cls.tearDownClass()
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise AssertionError('The UDisks LSM module should not be loaded')
        except dbus.exceptions.DBusException as e:
            # expect initialization failure for builtin config file defaults
            msg = r"Error initializing module 'lsm': LSM: Failed to connect plugin via URI"
            if not re.search(msg, e.get_dbus_message()):
                cls.tearDownClass()
                udiskstestcase.UdisksTestCase.tearDownClass()
                raise dbus.exceptions.DBusException('Unexpected error message received during initialization of unconfigured UDisks LSM module')

    @classmethod
    def tearDownClass(cls):
        udiskstestcase.UdisksTestCase.tearDownClass()
        if cls.lsm_module_conf_contents:
            # restore udisks2_lsm.conf contents
            cls.write_file(cls.lsm_module_conf_path, cls.lsm_module_conf_contents, ignore_nonexistent=False)
        else:
            # no previous udisks2_lsm.conf present, remove any leftovers
            cls.remove_file(cls.lsm_module_conf_path, ignore_nonexistent=True)

        if cls.lsm_db_file:
            cls.remove_file(cls.lsm_db_file, ignore_nonexistent=True)

    def setUp(self):
        self.lsm_device = self.vdevs[0]
        if not self.lsm_db_file:
            # create a lsm sim plugin db
            _fd, UdisksLSMTestCase.lsm_db_file = tempfile.mkstemp(dir='/var/tmp/', prefix='udisks_lsm_sim_db_')
            os.close(_fd)
            os.unlink(self.lsm_db_file)

            # add bogus volume
            ret, out = self.run_command("lsmcli -u 'sim://?statefile=%s' volume-create --name udisks_test_vol1 --size 1G --pool POOL_ID_00001" % self.lsm_db_file)
            if ret != 0:
                self.fail('Call to lsmcli failed: %s' % out)

            # need to chown the db file to root:root otherwise 'sqlite3' would complain
            stat_info = os.stat(self.lsm_db_file)
            os.chown(self.lsm_db_file, 0, 0)

            # spoof the VPD 0x83 for the testing block device
            wwn = self._get_wwn(self.lsm_device)
            ret, out = self.run_command("sqlite3 %s \"UPDATE volumes SET vpd83='%s' WHERE name='udisks_test_vol1';\"" % (self.lsm_db_file, wwn[2:]))
            if ret != 0:
                self.fail('Call to sqlite3 failed: %s' % out)
            os.chown(self.lsm_db_file, stat_info.st_uid, stat_info.st_gid)

            # create testing udisks2_lsm.conf
            contents = 'refresh_interval = 30\nenable_sim = false\nenable_hpsa = false\nextra_uris = ["sim://?statefile=%s"]\nextra_passwords = ["password"]\n' % self.lsm_db_file
            self.write_file(self.lsm_module_conf_path, contents)

            # load the udisks lsm module
            self.assertTrue(self.check_module_loaded('lsm'))

            UdisksLSMTestCase.lsm_init_done = True
        else:
            if not self.lsm_init_done:
                self.fail('Previous libstoragemgmt database initialization error, skipping')

        super(UdisksLSMTestCase, self).setUp()


    def _get_wwn(self, block_device):
        """
        Get WWN of a drive associated for the @block_device.
        """
        block = self.get_object('/block_devices/%s' % os.path.basename(block_device))
        self.assertIsNotNone(block)
        drive_obj_path = self.get_property_raw(block, '.Block', 'Drive')
        self.assertIsNotNone(drive_obj_path)
        drive = self.bus.get_object(self.iface_prefix, drive_obj_path)
        self.assertIsNotNone(drive)
        drive_object = dbus.Interface(drive, dbus_interface=self.iface_prefix + '.Drive')
        self.assertIsNotNone(drive_object)
        wwn = self.get_property_raw(drive, '.Drive', 'WWN')
        self.assertStartswith(wwn, '0x')
        return wwn

    def _get_drive_objects(self):
        """ Get all objects and filter drive objects """
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        return [p for p in list(objects.keys()) if "/drives/" in p]

    def test_drive_lsm_local(self):
        """
        Check the presence of .Drive.LsmLocal interface on all drive objects
        Expect to get a "org.freedesktop.UDisks2.Error.NotSupported" on any
        of the LED control methods as the libstoragemgmt sim plugin can't handle that
        """
        for obj_path in self._get_drive_objects():
            drive_object = self.get_object(obj_path)
            self.assertIsNotNone(drive_object)
            drive_lsm_local = dbus.Interface(drive_object, dbus_interface=self.iface_prefix + '.Drive.LsmLocal')
            self.assertIsNotNone(drive_lsm_local)

            # now call each of the LED control methods and let them fail
            for method_name in UdisksLSMTestCase._LED_CONTROL_METHOD_NAMES:
                method = drive_lsm_local.get_dbus_method(method_name)
                with six.assertRaisesRegex(self, dbus.exceptions.DBusException, r'Specified disk does not support this action'):
                    method(self.no_options)

    def test_drive_lsm(self):
        """
        Check the presence of .Drive.LSM interface on a tested drive
        Read all properties and compare their values against the mocked sim plugin environment.
        """
        # get WWN of the drive tested
        wwn = self._get_wwn(self.lsm_device)
        for obj_path in self._get_drive_objects():
            drive_object = self.get_object(obj_path)
            self.assertIsNotNone(drive_object)
            drive = dbus.Interface(drive_object, dbus_interface=self.iface_prefix + '.Drive')
            self.assertIsNotNone(drive)
            drive_wwn = self.get_property_raw(drive, '.Drive', 'WWN')
            self.assertStartswith(wwn, '0x')
            drive_lsm_local = dbus.Interface(drive_object, dbus_interface=self.iface_prefix + '.Drive.LsmLocal')
            self.assertIsNotNone(drive_lsm_local)
            drive_lsm = dbus.Interface(drive_object, dbus_interface=self.iface_prefix + '.Drive.LSM')
            self.assertIsNotNone(drive_lsm)

            if wwn == drive_wwn:
                self.assertTrue  (self.get_property_raw(drive, '.Drive.LSM', 'IsOK'))
                self.assertFalse (self.get_property_raw(drive, '.Drive.LSM', 'IsRaidDegraded'))
                self.assertFalse (self.get_property_raw(drive, '.Drive.LSM', 'IsRaidError'))
                self.assertFalse (self.get_property_raw(drive, '.Drive.LSM', 'IsRaidVerifying'))
                self.assertFalse (self.get_property_raw(drive, '.Drive.LSM', 'IsRaidReconstructing'))
                self.assertEquals(self.get_property_raw(drive, '.Drive.LSM', 'RaidType'), 'RAID 1')
                self.assertEquals(self.get_property_raw(drive, '.Drive.LSM', 'StatusInfo'), '')
                self.assertEquals(self.get_property_raw(drive, '.Drive.LSM', 'MinIoSize'), 512)
                self.assertEquals(self.get_property_raw(drive, '.Drive.LSM', 'OptIoSize'), 512)
                self.assertEquals(self.get_property_raw(drive, '.Drive.LSM', 'RaidDiskCount'), 2)
            else:
                # no .Drive.LSM interface should be present on other objects
                with six.assertRaisesRegex(self, dbus.exceptions.DBusException, r'org.freedesktop.DBus.Error.InvalidArgs: No such interface'):
                    self.get_property_raw(drive_lsm, '.Drive.LSM', 'IsOK')
