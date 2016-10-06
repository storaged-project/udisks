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
