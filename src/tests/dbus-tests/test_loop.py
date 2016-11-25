import time
import dbus
import os
import tempfile
import storagedtestcase


class StoragedLoopDeviceTest(storagedtestcase.StoragedTestCase):
    """This is LoopDevice related functions unit test"""

    LOOP_DEVICE_FILENAME = 'loop_device.img'

    def setUp(self):
        # create a file and fill it with some data
        self.run_command('dd if=/dev/zero of=%s bs=10MiB count=1' % self.LOOP_DEVICE_FILENAME)
        ret_code, self.dev_name = self.run_command('losetup --find --show %s' % self.LOOP_DEVICE_FILENAME)
        self.assertEqual(ret_code, 0)
        self.device = self.get_object('/block_devices/' + os.path.basename(self.dev_name))
        self.iface = dbus.Interface(self.device, dbus_interface=self.iface_prefix + '.Loop')

    def tearDown(self):
        # tear down loop device
        self.run_command('umount %s' % self.dev_name)
        self.run_command('losetup --detach %s' % self.dev_name)
        os.remove(self.LOOP_DEVICE_FILENAME)

    def test_10_delete(self):
        # check that loop device exists
        ret_code, result = self.run_command('losetup --list')
        self.assertEqual(ret_code, 0)
        if self.dev_name not in result:
            self.fail('Test loop device "%s" not found' % self.dev_name)
        # remove loop device
        self.iface.Delete(self.no_options)
        self.udev_settle()
        # check that loop device does not exist anymore
        ret_code, result = self.run_command('losetup --list')
        self.assertEqual(ret_code, 0)
        if self.dev_name in result:
            self.fail('Test loop device was not deleted' % self.dev_name)
        # TODO: Device is still present on Dbus even when detached. This is
        # probably a storaged and udisks2 issue. Not addressed for now to keep
        # the same storaged/udisks2 functionality (japokorn, Nov 2016)

    def test_20_setautoclear(self):
        # autoclear detaches loop device as soon as it is umounted
        # to be able to check that property value is set we need to mount the
        # device first
        flag_file_name = '/sys/class/block/%s/loop/autoclear' % os.path.basename(self.dev_name)
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.run_command('mkfs -t ext4 %s' % self.dev_name)
        self.run_command('mount %s %s' % (self.dev_name, tmp.name))
        self.iface.SetAutoclear(True, self.no_options)
        self.udev_settle()
        autoclear_flag = self.get_property(self.device, '.Loop', 'Autoclear')
        # property should be set now
        self.assertTrue(autoclear_flag)
        autoclear_flag = self.read_file(flag_file_name)
        self.assertEqual(autoclear_flag, '1\n')

    def test_30_backingfile(self):
        time.sleep(0.1)  # in this case SetUp does not always finish in time
        raw = self.get_property(self.device, '.Loop', 'BackingFile')
        # transcription from array of Bytes to string plus removal of trailing \0
        backing_file = self.ay_to_str(raw)
        self.assertEqual(os.path.join(os.getcwd(), self.LOOP_DEVICE_FILENAME), backing_file)

    def test_40_setupbyuid(self):
        time.sleep(0.1)  # in this case SetUp does not always finish in time
        uid = self.get_property(self.device, '.Loop', 'SetupByUID')
        self.assertEqual(uid, 0)  # uid should be 0 since device is not created by Udisks
