import time
import dbus
import glob
import os
import udiskstestcase


class UdisksDriveTest(udiskstestcase.UdisksTestCase):
    """This is Drive related functions unit test"""

    def get_drive(self, device):
        drive_name = self.get_drive_name(device)
        drive = self.get_object('/drives/' + os.path.basename(drive_name))
        drive_object = dbus.Interface(drive, dbus_interface=self.iface_prefix + '.Drive')
        return drive_object

    def setUp(self):
        # make sure scsi_debug is not loaded
        res, _ = self.run_command('rmmod scsi_debug')
        self.assertEqual(res, 1)
        # create new fake CD-ROM
        # ptype=5 - created device will be CD drive, one new target and host
        res, _ = self.run_command('modprobe scsi_debug ptype=5 num_tgts=1 add_host=1 removable=1')
        self.assertEqual(res, 0)
        self.run_command('udevadm trigger --settle')
        self.udev_settle()
        dirs = []
        # wait until directory appears
        while len(dirs) < 1:
            dirs = glob.glob('/sys/bus/pseudo/drivers/scsi_debug/adapter*/host*/target*/*:*/block')
            time.sleep(0.1)

        self.cd_dev = os.listdir(dirs[0])
        self.assertEqual(len(self.cd_dev), 1)
        obj = self.get_object('/block_devices/' + self.cd_dev[0])
        self.cd_dev = '/dev/' + self.cd_dev[0]
        self.assertTrue(os.path.exists(self.cd_dev))
        # wait for the corresponding block object to appear
        self.assertHasIface(obj, self.iface_prefix + '.Block')

        self.cd_device = self.get_device(self.cd_dev)
        self.cd_drive_name = self.get_drive_name(self.cd_device)
        self.cd_drive = self.get_drive(self.cd_device)

    def tearDown(self):
        device = self.cd_dev.split('/')[-1]
        if os.path.exists('/sys/block/' + device):
            self.write_file('/sys/block/%s/device/delete' % device, '1')
            while os.path.exists(device):
                time.sleep(0.1)
            self.udev_settle()
            res, _ = self.run_command('modprobe -r scsi_debug')
            self.assertEqual(res, 0)
            self.run_command('udevadm trigger --settle')

    def test_10_eject(self):
        ''' Test of Drive.Eject method '''

        dev = self.get_device(self.vdevs[0])
        drive = self.get_drive(dev)
        with self.assertRaisesRegex(dbus.exceptions.DBusException,
                                    r'is not hot-pluggable device|is not ejectable device'):
            drive.Eject(self.no_options)

        dev = self.get_device(self.cd_dev)
        drive = self.get_drive(dev)
        drive.Eject(self.no_options)

    def test_20_poweroff(self):
        ''' Test of Drive.PowerOff method '''
        for dev in (self.vdevs[0], self.cd_dev):
            device = self.get_device(dev)
            drive = self.get_drive(device)

            # check should fail since device cannot be powered off
            # sadly this is so far the only way we can test this function
            with self.assertRaisesRegex(dbus.exceptions.DBusException,
                                        'Failed: No usb device'):
                drive.PowerOff(self.no_options)

    def test_30_setconfiguration(self):
        ''' Test of Drive.SetConfiguration method '''
        # set configuration value to some improbable value
        self.cd_drive.SetConfiguration({'ata-pm-standby': 286}, self.no_options)

        # validate that configuration property has changed
        conf_value = self.get_property(self.cd_drive, '.Drive', 'Configuration')
        conf_value.assertIsNotNone()
        self.assertEqual(int(conf_value.value['ata-pm-standby']), 286)

    def test_40_properties(self):
        ''' Test of Drive properties values '''

        sys_dirs = glob.glob('/sys/bus/pseudo/drivers/scsi_debug/adapter*/host*/target*/*:*/')
        print(sys_dirs)
        self.assertEqual(len(sys_dirs), 1)
        sys_dir = sys_dirs[0]
        print(sys_dir)

        def read_sys_file(value):
            return self.read_file(os.path.join(sys_dir, value)).strip()

        print(self.cd_dev)
        ret_code, wwn = self.run_command('lsblk -d -no WWN %s' % self.cd_dev)
        self.assertEqual(ret_code, 0)

        rotational = read_sys_file("block/%s/queue/rotational" % os.path.basename(self.cd_dev))
        print(rotational)

        res, _ = self.run_command('udisksctl dump')
        print (_)


        # values expected are preset by scsi_debug and do not change
        expected_prop_vals = {
            'MediaCompatibility': ['optical_cd'],
            'Ejectable': 1,
            'MediaAvailable': 1,
            'MediaChangeDetected': 1,
            'MediaRemovable': 1,
            'Optical': 1,
            'Removable': 1,
            'RotationRate': -1 if rotational == "1" else 0,
            'Media': 'optical_cd',
            'Model': read_sys_file('model'),
            'Revision': read_sys_file('rev'),
            'Seat': 'seat0',
            'Vendor': read_sys_file('vendor'),
            'WWN': wwn,
            'OpticalNumDataTracks': 1,
            'OpticalNumTracks': 1,
            'Size': 8388608
        }

        for prop_name, expected_val in expected_prop_vals.items():
            actual_val = self.get_property(self.cd_drive, '.Drive', prop_name)
            actual_val.assertEqual(expected_val)

        # timeDetected and TimeMediaDetected has the same value and SortKey value
        # is derived from it
        timedetected = self.get_property(self.cd_drive, '.Drive', 'TimeDetected')
        timemediadetected = self.get_property(self.cd_drive, '.Drive', 'TimeMediaDetected')
        self.assertEqual(timedetected.value, timemediadetected.value)
        sortkey = self.get_property(self.cd_drive, '.Drive', 'SortKey')
        sortkey.assertEqual('01hotplug/%d' % timedetected.value)
