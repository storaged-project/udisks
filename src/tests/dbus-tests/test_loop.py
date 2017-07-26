import time
import dbus
import os
import shutil
import tempfile
import udiskstestcase


class UdisksLoopDeviceTest(udiskstestcase.UdisksTestCase):
    """Unit tests for the Loop interface of loop devices"""

    LOOP_DEVICE_FILENAME = 'loop_device.img'

    def setUp(self):
        # create a file and fill it with some data
        self.run_command('dd if=/dev/zero of=%s bs=10MiB count=1' % self.LOOP_DEVICE_FILENAME)
        ret_code, self.dev_name = self.run_command('losetup --find --show %s' % self.LOOP_DEVICE_FILENAME)
        self.assertEqual(ret_code, 0)
        time.sleep(0.5)
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
        # probably a udisks and udisks2 issue. Not addressed for now to keep
        # the same udisks/udisks2 functionality (japokorn, Nov 2016)

    def test_20_setautoclear(self):
        # autoclear detaches loop device as soon as it is umounted
        # to be able to check that property value is set we need to mount the
        # device first
        flag_file_name = '/sys/class/block/%s/loop/autoclear' % os.path.basename(self.dev_name)
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        self.run_command('mkfs -t ext4 %s' % self.dev_name)
        self.run_command('mount %s %s' % (self.dev_name, tmp))
        self.iface.SetAutoclear(True, self.no_options)
        self.udev_settle()
        autoclear_flag = self.get_property(self.device, '.Loop', 'Autoclear')
        # property should be set now
        autoclear_flag.assertTrue()
        autoclear_flag = self.read_file(flag_file_name)
        self.assertEqual(autoclear_flag, '1\n')

    def test_30_backingfile(self):
        raw = self.get_property(self.device, '.Loop', 'BackingFile')
        # transcription to array of Bytes to string plus adding the trailing \0
        backing_file = self.str_to_ay(os.path.join(os.getcwd(), self.LOOP_DEVICE_FILENAME))
        raw.assertEqual(backing_file)

    def test_40_setupbyuid(self):
        uid = self.get_property(self.device, '.Loop', 'SetupByUID')
        uid.assertEqual(0)  # uid should be 0 since device is not created by Udisks

class UdisksManagerLoopDeviceTest(udiskstestcase.UdisksTestCase):
    """Unit tests for the loop-related methods of the Manager object"""

    LOOP_DEVICE_FILENAME = 'loop_device.img'

    def setUp(self):
        # create a file and fill it with some data
        self.run_command('dd if=/dev/zero of=%s bs=10MiB count=1' % self.LOOP_DEVICE_FILENAME)
        self.addCleanup(os.remove, self.LOOP_DEVICE_FILENAME)
        self.manager = self.get_interface("/Manager", ".Manager")

    def test_10_create_simple(self):
        with open(self.LOOP_DEVICE_FILENAME, "r+b") as loop_file:
            fd = loop_file.fileno()
            loop_dev_obj_path = self.manager.LoopSetup(fd, self.no_options)
        self.assertTrue(loop_dev_obj_path)
        self.assertTrue(loop_dev_obj_path.startswith(self.path_prefix))
        path, loop_dev = loop_dev_obj_path.rsplit("/", 1)
        self.addCleanup(self.run_command, "losetup -d /dev/%s" % loop_dev)

        loop_dev_obj = self.get_object(loop_dev_obj_path)

        # should use the right backing file
        raw = self.get_property(loop_dev_obj, '.Loop', 'BackingFile')
        # transcription to array of Bytes to string plus adding the trailing \0
        backing_file = self.str_to_ay(os.path.join(os.getcwd(), self.LOOP_DEVICE_FILENAME))
        raw.assertEqual(backing_file)

        # should use the whole file
        size = self.get_property(loop_dev_obj, ".Block", "Size")
        size.assertEqual(10 * 1024**2)

        # should be writable
        ro = self.get_property(loop_dev_obj, ".Block", "ReadOnly")
        ro.assertFalse()

        # should be set up by root (uid 0)
        uid = self.get_property(loop_dev_obj, '.Loop', 'SetupByUID')
        uid.assertEqual(0)

    def test_20_create_with_offset(self):
        opts = dbus.Dictionary({"offset": dbus.UInt64(4096)}, signature=dbus.Signature('sv'))
        with open(self.LOOP_DEVICE_FILENAME, "r+b") as loop_file:
            fd = loop_file.fileno()
            loop_dev_obj_path = self.manager.LoopSetup(fd, opts)
        self.assertTrue(loop_dev_obj_path)
        self.assertTrue(loop_dev_obj_path.startswith(self.path_prefix))
        path, loop_dev = loop_dev_obj_path.rsplit("/", 1)
        self.addCleanup(self.run_command, "losetup -d /dev/%s" % loop_dev)

        loop_dev_obj = self.get_object(loop_dev_obj_path)

        # should use the right backing file
        raw = self.get_property(loop_dev_obj, '.Loop', 'BackingFile')
        # transcription to array of Bytes to string plus adding the trailing \0
        backing_file = self.str_to_ay(os.path.join(os.getcwd(), self.LOOP_DEVICE_FILENAME))
        raw.assertEqual(backing_file)

        # should use the whole file except for the first 4096 bytes (offset)
        size = self.get_property(loop_dev_obj, ".Block", "Size")
        size.assertEqual(10 * 1024**2 - 4096)

        # should be writable
        ro = self.get_property(loop_dev_obj, ".Block", "ReadOnly")
        ro.assertFalse()

    def test_30_create_with_offset_size(self):
        opts = dbus.Dictionary({"offset": dbus.UInt64(4096), "size": dbus.UInt64(4 * 1024**2)}, signature=dbus.Signature('sv'))
        with open(self.LOOP_DEVICE_FILENAME, "r+b") as loop_file:
            fd = loop_file.fileno()
            loop_dev_obj_path = self.manager.LoopSetup(fd, opts)
        self.assertTrue(loop_dev_obj_path)
        self.assertTrue(loop_dev_obj_path.startswith(self.path_prefix))
        path, loop_dev = loop_dev_obj_path.rsplit("/", 1)
        self.addCleanup(self.run_command, "losetup -d /dev/%s" % loop_dev)

        loop_dev_obj = self.get_object(loop_dev_obj_path)

        # should use the right backing file
        raw = self.get_property(loop_dev_obj, '.Loop', 'BackingFile')
        # transcription to array of Bytes to string plus adding the trailing \0
        backing_file = self.str_to_ay(os.path.join(os.getcwd(), self.LOOP_DEVICE_FILENAME))
        raw.assertEqual(backing_file)

        # should use just the space specified by the 'size' argument
        size = self.get_property(loop_dev_obj, ".Block", "Size")
        size.assertEqual(4 * 1024**2)

        # should be writable
        ro = self.get_property(loop_dev_obj, ".Block", "ReadOnly")
        ro.assertFalse()

    def test_40_create_read_only(self):
        opts = dbus.Dictionary({"read-only": dbus.Boolean(True)}, signature=dbus.Signature('sv'))
        with open(self.LOOP_DEVICE_FILENAME, "r+b") as loop_file:
            fd = loop_file.fileno()
            loop_dev_obj_path = self.manager.LoopSetup(fd, opts)
        self.assertTrue(loop_dev_obj_path)
        self.assertTrue(loop_dev_obj_path.startswith(self.path_prefix))
        path, loop_dev = loop_dev_obj_path.rsplit("/", 1)
        self.addCleanup(self.run_command, "losetup -d /dev/%s" % loop_dev)

        loop_dev_obj = self.get_object(loop_dev_obj_path)

        # should use the right backing file
        raw = self.get_property(loop_dev_obj, '.Loop', 'BackingFile')
        # transcription to array of Bytes to string plus adding the trailing \0
        backing_file = self.str_to_ay(os.path.join(os.getcwd(), self.LOOP_DEVICE_FILENAME))
        raw.assertEqual(backing_file)

        # should use the whole file
        size = self.get_property(loop_dev_obj, ".Block", "Size")
        size.assertEqual(10 * 1024**2)

        # should be read-only
        ro = self.get_property(loop_dev_obj, ".Block", "ReadOnly")
        ro.assertTrue()

    def test_50_create_no_part_scan(self):
        # create a partition on the file (future loop device)
        ret, out = self.run_command("parted %s mklabel msdos" % self.LOOP_DEVICE_FILENAME)
        self.assertEqual(ret, 0)
        ret, out = self.run_command("parted %s mkpart primary ext2 1 10" % self.LOOP_DEVICE_FILENAME)
        self.assertEqual(ret, 0)

        opts = dbus.Dictionary({"no-part-scan": dbus.Boolean(True)}, signature=dbus.Signature('sv'))
        with open(self.LOOP_DEVICE_FILENAME, "r+b") as loop_file:
            fd = loop_file.fileno()
            loop_dev_obj_path = self.manager.LoopSetup(fd, opts)
        self.assertTrue(loop_dev_obj_path)
        self.assertTrue(loop_dev_obj_path.startswith(self.path_prefix))
        path, loop_dev = loop_dev_obj_path.rsplit("/", 1)
        self.addCleanup(self.run_command, "losetup -d /dev/%s" % loop_dev)

        loop_dev_obj = self.get_object(loop_dev_obj_path)

        # should use the right backing file
        raw = self.get_property(loop_dev_obj, '.Loop', 'BackingFile')
        # transcription to array of Bytes to string plus adding the trailing \0
        backing_file = self.str_to_ay(os.path.join(os.getcwd(), self.LOOP_DEVICE_FILENAME))
        raw.assertEqual(backing_file)

        # should use the whole file except for the first 4096 bytes (offset)
        size = self.get_property(loop_dev_obj, ".Block", "Size")
        size.assertEqual(10 * 1024**2)

        # should be writable
        ro = self.get_property(loop_dev_obj, ".Block", "ReadOnly")
        ro.assertFalse()

        # partitions shouldn't be scanned
        self.assertFalse(os.path.exists("/dev/%sp1" % loop_dev))

        # detach the file an try it again, this time requesting the partitions to be scanned
        self.run_command("losetup -d /dev/%s" % loop_dev)

        opts = dbus.Dictionary({"no-part-scan": dbus.Boolean(False)}, signature=dbus.Signature('sv'))
        with open(self.LOOP_DEVICE_FILENAME, "r+b") as loop_file:
            fd = loop_file.fileno()
            loop_dev_obj_path = self.manager.LoopSetup(fd, opts)
        self.assertTrue(loop_dev_obj_path)
        self.assertTrue(loop_dev_obj_path.startswith(self.path_prefix))
        path, loop_dev = loop_dev_obj_path.rsplit("/", 1)
        self.addCleanup(self.run_command, "losetup -d /dev/%s" % loop_dev)

        # partitions should be scanned
        self.assertTrue(os.path.exists("/dev/%sp1" % loop_dev))
