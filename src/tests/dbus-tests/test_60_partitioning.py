import dbus
import os
import subprocess

import storagedtestcase


BLOCK_SIZE = 512


def read_file(filename):
    with open(filename, 'r') as f:
        content = f.read()
    return content


def run_command(command):
    out = subprocess.check_output(command, shell=True)
    return out.decode().strip()


class StoragedPartitionTableTest(storagedtestcase.StoragedTestCase):
    '''This is a basic block device test suite'''

    def _remove_format(self, device):
        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        device.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

    def _create_format(self, device, ftype):
        device.Format(ftype, self.no_options, dbus_interface=self.iface_prefix + '.Block')

    def _remove_partition(self, part):
        part.Delete(self.no_options, dbus_interface=self.iface_prefix + '.Partition')

    def test_create_mbr_partition(self):
        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create msdos partition table
        self._create_format(disk, 'dos')
        self.addCleanup(self._remove_format, disk)

        pttype = self.get_property(disk, '.PartitionTable', 'Type')
        self.assertEqual(pttype, 'dos')

        # create partition
        path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(100 * 1024**2), '', '',
                                    self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        self.addCleanup(self._remove_partition, part)

        # check dbus properties
        size = self.get_property(part, '.Partition', 'Size')
        self.assertEqual(size, 100 * 1024**2)

        offset = self.get_property(part, '.Partition', 'Offset')
        self.assertEqual(offset, 2 * 1024**2)  # storaged adds 1 MiB to partition start

        # check system values
        part_name = path.split('/')[-1]
        disk_name = os.path.basename(self.vdevs[0])
        part_syspath = '/sys/block/%s/%s' % (disk_name, part_name)
        self.assertTrue(os.path.isdir(part_syspath))

        sys_size = int(read_file('%s/size' % part_syspath))
        self.assertEqual(sys_size * BLOCK_SIZE, 100 * 1024**2)

        sys_start = int(read_file('%s/start' % part_syspath))
        self.assertEqual(sys_start * BLOCK_SIZE, 2 * 1024**2)

    def test_create_extended_partition(self):

        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create msdos partition table
        self._create_format(disk, 'dos')
        self.addCleanup(self._remove_format, disk)

        # create extended partition
        ext_path = disk.CreatePartition(dbus.UInt64(0), dbus.UInt64(100 * 1024**2), '0x0f', '',
                                        self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        ext_part = self.bus.get_object(self.iface_prefix, ext_path)
        self.assertIsNotNone(ext_part)

        self.addCleanup(self._remove_partition, ext_part)

        # check dbus type and check if its a 'container'
        dbus_pttype = self.get_property(ext_part, '.Partition', 'Type')
        self.assertEqual(dbus_pttype, '0x0f')

        dbus_cont = self.get_property(ext_part, '.Partition', 'IsContainer')
        self.assertTrue(dbus_cont)

        # check system type
        part_name = str(ext_part.object_path).split('/')[-1]
        sys_pttype = run_command('lsblk -no PARTTYPE /dev/%s' % part_name)
        self.assertEqual(sys_pttype, '0xf')  # lsblk prints 0xf instead of 0x0f

        # create logical partition
        log_path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(50 * 1024**2), '', '',
                                        self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        log_part = self.bus.get_object(self.iface_prefix, log_path)
        self.assertIsNotNone(log_part)

        self.addCleanup(self._remove_partition, log_part)

        # check if its a 'contained'
        dbus_cont = self.get_property(log_part, '.Partition', 'IsContained')
        self.assertTrue(dbus_cont)

    def test_create_gpt_partition(self):
        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create gpt partition table
        self._create_format(disk, 'gpt')
        pttype = self.get_property(disk, '.PartitionTable', 'Type')
        self.assertEqual(pttype, 'gpt')

        self.addCleanup(self._remove_format, disk)

        # create partition
        path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(100 * 1024**2), '', '',
                                    self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')

        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        self.addCleanup(self._remove_partition, part)

        # check dbus properties
        size = self.get_property(part, '.Partition', 'Size')
        self.assertEqual(size, 100 * 1024**2)

        offset = self.get_property(part, '.Partition', 'Offset')
        self.assertEqual(offset, 2 * 1024**2)  # storaged adds 1 MiB to partition start

        # check system values
        part_name = path.split('/')[-1]
        disk_name = os.path.basename(self.vdevs[0])
        part_syspath = '/sys/block/%s/%s' % (disk_name, part_name)
        self.assertTrue(os.path.isdir(part_syspath))

        sys_size = int(read_file('%s/size' % part_syspath))
        self.assertEqual(sys_size * BLOCK_SIZE, 100 * 1024**2)

        sys_start = int(read_file('%s/start' % part_syspath))
        self.assertEqual(sys_start * BLOCK_SIZE, 2 * 1024**2)

    def test_create_with_format(self):
        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create msdos partition table
        self._create_format(disk, 'dos')

        self.addCleanup(self._remove_format, disk)

        # create partition with xfs format
        path = disk.CreatePartitionAndFormat(dbus.UInt64(1024**2), dbus.UInt64(100 * 1024**2), '', '',
                                             self.no_options, 'xfs', self.no_options,
                                             dbus_interface=self.iface_prefix + '.PartitionTable')

        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        self.addCleanup(self._remove_partition, part)
        self.addCleanup(self._remove_format, part)

        # check dbus properties
        size = self.get_property(part, '.Partition', 'Size')
        self.assertEqual(size, 100 * 1024**2)

        offset = self.get_property(part, '.Partition', 'Offset')
        self.assertEqual(offset, 2 * 1024**2)  # storaged adds 1 MiB to partition start

        usage = self.get_property(part, '.Block', 'IdUsage')
        self.assertEqual(usage, 'filesystem')

        fstype = self.get_property(part, '.Block', 'IdType')
        self.assertEqual(fstype, 'xfs')

        # check system values
        part_name = path.split('/')[-1]
        disk_name = os.path.basename(self.vdevs[0])
        part_syspath = '/sys/block/%s/%s' % (disk_name, part_name)
        self.assertTrue(os.path.isdir(part_syspath))

        sys_size = int(read_file('%s/size' % part_syspath))
        self.assertEqual(sys_size * BLOCK_SIZE, 100 * 1024**2)

        sys_start = int(read_file('%s/start' % part_syspath))
        self.assertEqual(sys_start * BLOCK_SIZE, 2 * 1024**2)

        sys_fstype = run_command('lsblk -no FSTYPE /dev/%s' % part_name)
        self.assertEqual(sys_fstype, 'xfs')


class StoragedPartitionTest(storagedtestcase.StoragedTestCase):
    '''This is a basic partition test suite'''

    def _remove_format(self, device):
        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        device.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

    def _create_format(self, device, ftype):
        device.Format(ftype, self.no_options, dbus_interface=self.iface_prefix + '.Block')

    def _remove_partition(self, device):
        device.Delete(self.no_options, dbus_interface=self.iface_prefix + '.Partition')

    def _create_partition(self, disk, start=1024**2, size=100 * 1024**2, fmt='xfs'):
        if fmt:
            path = disk.CreatePartitionAndFormat(dbus.UInt64(start), dbus.UInt64(size), '', '',
                                                 self.no_options, fmt, self.no_options,
                                                 dbus_interface=self.iface_prefix + '.PartitionTable')
        else:
            path = disk.CreatePartition(dbus.UInt64(start), dbus.UInt64(size), '', '',
                                        self.no_options,
                                        dbus_interface=self.iface_prefix + '.PartitionTable')

        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        return part

    def test_delete(self):
        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        self._create_format(disk, 'dos')
        self.addCleanup(self._remove_format, disk)

        part = self._create_partition(disk)
        path = str(part.object_path)

        part.Delete(self.no_options, dbus_interface=self.iface_prefix + '.Partition')

        self.udev_settle()

        part_name = path.split('/')[-1]
        disk_name = os.path.basename(self.vdevs[0])
        part_syspath = '/sys/block/%s/%s' % (disk_name, part_name)

        # make sure the partition is not on dbus
        udisks = self.get_object('', '')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(path, objects.keys())

        # make sure partition is not in the system
        self.assertFalse(os.path.isdir(part_syspath))

    def test_flags(self):
        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        self._create_format(disk, 'dos')
        self.addCleanup(self._remove_format, disk)

        part = self._create_partition(disk)
        self.addCleanup(self._remove_partition, part)

        self._create_format(part, 'xfs')
        self.addCleanup(self._remove_format, part)

        # set boot flag (10000000(2), 128(10), 0x80(16))
        part.SetFlags(dbus.UInt64(10000000), self.no_options,
                      dbus_interface=self.iface_prefix + '.Partition')
        self.udev_settle()

        # test flags value on types
        dbus_flags = self.get_property(part, '.Partition', 'Flags')
        self.assertEqual(dbus_flags, 128)

        # test flags value from sysytem
        part_name = str(part.object_path).split('/')[-1]
        sys_flags = run_command('lsblk -no PARTFLAGS /dev/%s' % part_name)
        self.assertEqual(sys_flags, '0x80')

    def test_type(self):
        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        self._create_format(disk, 'gpt')
        self.addCleanup(self._remove_format, disk)

        part = self._create_partition(disk)
        self.addCleanup(self._remove_partition, part)

        self._create_format(part, 'xfs')
        self.addCleanup(self._remove_format, part)

        # set part type/guid (home partition)
        home_guid = '933ac7e1-2eb4-4f13-b844-0e14e2aef915'
        part.SetType(home_guid, self.no_options,
                     dbus_interface=self.iface_prefix + '.Partition')
        self.udev_settle()

        # test flags value on types
        dbus_type = self.get_property(part, '.Partition', 'Type')
        self.assertEqual(dbus_type, home_guid)

        # test flags value from sysytem
        part_name = str(part.object_path).split('/')[-1]
        sys_type = run_command('lsblk -no PARTTYPE /dev/%s' % part_name)
        self.assertEqual(sys_type, home_guid)

    def test_name(self):
        disk = self.get_object('', '/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        self._create_format(disk, 'gpt')
        self.addCleanup(self._remove_format, disk)

        part = self._create_partition(disk)
        self.addCleanup(self._remove_partition, part)

        self._create_format(part, 'xfs')
        self.addCleanup(self._remove_format, part)

        # set part name
        part.SetName('test', self.no_options,
                     dbus_interface=self.iface_prefix + '.Partition')

        self.udev_settle()

        # test flags value on types
        dbus_name = self.get_property(part, '.Partition', 'Name')
        self.assertEqual(dbus_name, 'test')

        # test flags value from sysytem
        part_name = str(part.object_path).split('/')[-1]
        sys_name = run_command('lsblk -no PARTLABEL /dev/%s' % part_name)
        self.assertEqual(sys_name, 'test')
