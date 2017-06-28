import dbus
import os
import six
import time

import udiskstestcase


BLOCK_SIZE = 512


class UdisksPartitionTableTest(udiskstestcase.UdisksTestCase):
    '''This is a basic block device test suite'''

    def _remove_format(self, device):
        d = dbus.Dictionary(signature='sv')
        d['erase'] = True
        device.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')

    def _create_format(self, device, ftype):
        device.Format(ftype, self.no_options, dbus_interface=self.iface_prefix + '.Block')

    def _remove_partition(self, part):
        try:
            part.Delete(self.no_options, dbus_interface=self.iface_prefix + '.Partition')
        except dbus.exceptions.DBusException:
            time.sleep(1)
            part.Delete(self.no_options, dbus_interface=self.iface_prefix + '.Partition')

    def test_create_mbr_partition(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create msdos partition table
        self._create_format(disk, 'dos')
        self.addCleanup(self._remove_format, disk)

        pttype = self.get_property(disk, '.PartitionTable', 'Type')
        pttype.assertEqual('dos')

        part_type = '0x8e'  # 'Linux LVM' type, see https://en.wikipedia.org/wiki/Partition_type#PID_8Eh

        # first try to create partition with name -> should fail because mbr
        # doesn't support partition names
        msg = 'MBR partition table does not support names'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(100 * 1024**2), part_type, 'name',
                                        self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')

        # create partition
        path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(100 * 1024**2), part_type, '',
                                    self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        self.addCleanup(self._remove_partition, part)

        # check dbus properties
        size = self.get_property(part, '.Partition', 'Size')
        size.assertEqual(100 * 1024**2)

        offset = self.get_property(part, '.Partition', 'Offset')
        offset.assertEqual(1024**2)

        dbus_type = self.get_property(part, '.Partition', 'Type')
        dbus_type.assertEqual(part_type)

        # check system values
        part_name = path.split('/')[-1]
        disk_name = os.path.basename(self.vdevs[0])
        part_syspath = '/sys/block/%s/%s' % (disk_name, part_name)
        self.assertTrue(os.path.isdir(part_syspath))

        sys_size = int(self.read_file('%s/size' % part_syspath))
        self.assertEqual(sys_size * BLOCK_SIZE, 100 * 1024**2)

        sys_start = int(self.read_file('%s/start' % part_syspath))
        self.assertEqual(sys_start * BLOCK_SIZE, 1024**2)

        # format the partition so blkid is able to display info about it
        # (yes, it is stupid, but this is how blkid works on CentOS/RHEL 7)
        _ret, _out = self.run_command('mkfs.ext2 /dev/%s' % part_name)

        _ret, sys_type = self.run_command('blkid /dev/%s -p -o value -s PART_ENTRY_TYPE' % part_name)
        self.assertEqual(sys_type, part_type)

        # check uuid and part number
        dbus_uuid = self.get_property(part, '.Partition', 'UUID')
        _ret, sys_uuid = self.run_command('lsblk -d -no PARTUUID /dev/%s' % part_name)
        dbus_uuid.assertEqual(sys_uuid)

        dbus_num = self.get_property(part, '.Partition', 'Number')
        sys_num = int(self.read_file('%s/partition' % part_syspath))
        dbus_num.assertEqual(sys_num)

        # create another partition
        path = disk.CreatePartition(dbus.UInt64(1024**2 + (1024**2 + 100 * 1024**2)), dbus.UInt64(100 * 1024**2),
                                    part_type, '', self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        self.addCleanup(self._remove_partition, part)

        # create yet another partition
        path = disk.CreatePartition(dbus.UInt64(1024**2 + 2 * (1024**2 + 100 * 1024**2)), dbus.UInt64(100 * 1024**2),
                                    part_type, '', self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        self.addCleanup(self._remove_partition, part)

    def test_create_extended_partition(self):

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create msdos partition table
        self._create_format(disk, 'dos')
        self.addCleanup(self._remove_format, disk)

        # create extended partition
        ext_path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(150 * 1024**2), '0x05', '',
                                        self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        ext_part = self.bus.get_object(self.iface_prefix, ext_path)
        self.assertIsNotNone(ext_part)

        self.addCleanup(self._remove_partition, ext_part)

        # check dbus type (0x05, 0x0f, 0x85 are all exteded types, see https://en.wikipedia.org/wiki/Partition_type#PID_05h)
        dbus_pttype = self.get_property(ext_part, '.Partition', 'Type')
        dbus_pttype.assertIn(['0x05', '0x0f', '0x85'])

        # check if its a 'container'
        dbus_cont = self.get_property(ext_part, '.Partition', 'IsContainer')
        dbus_cont.assertTrue()

        # check system type
        part_name = str(ext_part.object_path).split('/')[-1]
        _ret, sys_pttype = self.run_command('blkid /dev/%s -p -o value -s PART_ENTRY_TYPE' % part_name)
        self.assertIn(sys_pttype, ['0x5', '0xf', '0x85'])  # lsblk prints 0xf instead of 0x0f

        # create logical partition
        log_path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(50 * 1024**2), '', '',
                                        self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        log_part = self.bus.get_object(self.iface_prefix, log_path)
        self.assertIsNotNone(log_part)

        self.addCleanup(self._remove_partition, log_part)

        # check if its a 'contained'
        dbus_cont = self.get_property(log_part, '.Partition', 'IsContained')
        dbus_cont.assertTrue()

        # create one more logical partition
        log_path2 = disk.CreatePartition(dbus.UInt64(51 * 1024**2), dbus.UInt64(50 * 1024**2), '', '',
                                         self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')
        self.udev_settle()

        log_part2 = self.bus.get_object(self.iface_prefix, log_path2)
        self.assertIsNotNone(log_part2)

        self.addCleanup(self._remove_partition, log_part)

        # check if its a 'contained'
        dbus_cont = self.get_property(log_part2, '.Partition', 'IsContained')
        dbus_cont.assertTrue()

    def test_create_gpt_partition(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create gpt partition table
        self._create_format(disk, 'gpt')
        pttype = self.get_property(disk, '.PartitionTable', 'Type')
        pttype.assertEqual('gpt')

        self.addCleanup(self._remove_format, disk)

        gpt_name = 'home'
        gpt_type = '933ac7e1-2eb4-4f13-b844-0e14e2aef915'

        # create partition
        path = disk.CreatePartition(dbus.UInt64(1024**2), dbus.UInt64(100 * 1024**2),
                                    gpt_type, gpt_name,
                                    self.no_options, dbus_interface=self.iface_prefix + '.PartitionTable')

        self.udev_settle()
        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        self.addCleanup(self._remove_partition, part)

        # check dbus properties
        size = self.get_property(part, '.Partition', 'Size')
        size.assertEqual(100 * 1024**2)

        offset = self.get_property(part, '.Partition', 'Offset')
        offset.assertEqual(1024**2)

        dbus_name = self.get_property(part, '.Partition', 'Name')
        dbus_name.assertEqual(gpt_name)

        dbus_type = self.get_property(part, '.Partition', 'Type')
        dbus_type.assertEqual(gpt_type)

        # check system values
        part_name = path.split('/')[-1]
        disk_name = os.path.basename(self.vdevs[0])
        part_syspath = '/sys/block/%s/%s' % (disk_name, part_name)
        self.assertTrue(os.path.isdir(part_syspath))

        sys_size = int(self.read_file('%s/size' % part_syspath))
        self.assertEqual(sys_size * BLOCK_SIZE, 100 * 1024**2)

        sys_start = int(self.read_file('%s/start' % part_syspath))
        self.assertEqual(sys_start * BLOCK_SIZE, 1024**2)

        _ret, sys_name = self.run_command('lsblk -d -no PARTLABEL /dev/%s' % part_name)
        self.assertEqual(sys_name, gpt_name)

        # format the partition so blkid is able to display info about it
        # (yes, it is stupid, but this is how blkid works on CentOS/RHEL 7)
        _ret, _out = self.run_command('mkfs.ext2 /dev/%s' % part_name)

        _ret, sys_type = self.run_command('blkid /dev/%s -p -o value -s PART_ENTRY_TYPE' % part_name)
        self.assertEqual(sys_type, gpt_type)

    def test_create_with_format(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
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
        size.assertEqual(100 * 1024**2)

        offset = self.get_property(part, '.Partition', 'Offset')
        offset.assertEqual(1024**2)

        usage = self.get_property(part, '.Block', 'IdUsage')
        usage.assertEqual('filesystem')

        fstype = self.get_property(part, '.Block', 'IdType')
        fstype.assertEqual('xfs')

        # check system values
        part_name = path.split('/')[-1]
        disk_name = os.path.basename(self.vdevs[0])
        part_syspath = '/sys/block/%s/%s' % (disk_name, part_name)
        self.assertTrue(os.path.isdir(part_syspath))

        sys_size = int(self.read_file('%s/size' % part_syspath))
        self.assertEqual(sys_size * BLOCK_SIZE, 100 * 1024**2)

        sys_start = int(self.read_file('%s/start' % part_syspath))
        self.assertEqual(sys_start * BLOCK_SIZE, 1024**2)

        _ret, sys_fstype = self.run_command('lsblk -d -no FSTYPE /dev/%s' % part_name)
        self.assertEqual(sys_fstype, 'xfs')


class UdisksPartitionTest(udiskstestcase.UdisksTestCase):
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

        self.udev_settle()
        part = self.bus.get_object(self.iface_prefix, path)
        self.assertIsNotNone(part)

        return part

    def test_delete(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
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
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(path, objects.keys())

        # make sure partition is not in the system
        self.assertFalse(os.path.isdir(part_syspath))

    def test_flags(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
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
        dbus_flags.assertEqual(128)

        # test flags value from sysytem
        part_name = str(part.object_path).split('/')[-1]
        _ret, sys_flags = self.run_command('blkid /dev/%s -p -o value -s PART_ENTRY_FLAGS' % part_name)
        self.assertEqual(sys_flags, '0x80')

    def test_gpt_type(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        self._create_format(disk, 'gpt')
        self.addCleanup(self._remove_format, disk)

        part = self._create_partition(disk)
        self.addCleanup(self._remove_partition, part)

        self._create_format(part, 'xfs')
        self.addCleanup(self._remove_format, part)

        # first try some invalid guid
        msg = 'org.freedesktop.UDisks2.Error.Failed: .* is not a valid UUID'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            part.SetType('aaaa', self.no_options,
                         dbus_interface=self.iface_prefix + '.Partition')

        # set part type/guid (home partition)
        home_guid = '933ac7e1-2eb4-4f13-b844-0e14e2aef915'
        part.SetType(home_guid, self.no_options,
                     dbus_interface=self.iface_prefix + '.Partition')
        self.udev_settle()

        # test flags value on types
        dbus_type = self.get_property(part, '.Partition', 'Type')
        dbus_type.assertEqual(home_guid)

        # test flags value from sysytem
        part_name = str(part.object_path).split('/')[-1]
        _ret, sys_type = self.run_command('blkid /dev/%s -p -o value -s PART_ENTRY_TYPE' % part_name)
        self.assertEqual(sys_type, home_guid)

    def test_dos_type(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        self._create_format(disk, 'dos')
        self.addCleanup(self._remove_format, disk)

        part = self._create_partition(disk)
        self.addCleanup(self._remove_partition, part)

        self._create_format(part, 'xfs')
        self.addCleanup(self._remove_format, part)

        # try to set part type to an extended partition type -- should fail
        msg = 'Refusing to change partition type to that of an extended partition'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            part.SetType('0x05', self.no_options,
                         dbus_interface=self.iface_prefix + '.Partition')

        # set part type/id
        part_type = '0x8e'   # 'Linux LVM' type, see https://en.wikipedia.org/wiki/Partition_type#PID_8Eh
        part.SetType(part_type, self.no_options,
                     dbus_interface=self.iface_prefix + '.Partition')
        self.udev_settle()

        # test flags value on types
        dbus_type = self.get_property(part, '.Partition', 'Type')
        dbus_type.assertEqual(part_type)

        # test flags value from sysytem
        part_name = str(part.object_path).split('/')[-1]
        _ret, sys_type = self.run_command('blkid /dev/%s -p -o value -s PART_ENTRY_TYPE' % part_name)
        self.assertEqual(sys_type, part_type)

    def test_name(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        self._create_format(disk, 'gpt')
        self.addCleanup(self._remove_format, disk)

        part = self._create_partition(disk)
        self.addCleanup(self._remove_partition, part)

        self._create_format(part, 'xfs')
        self.addCleanup(self._remove_format, part)

        # first try some invalid name (longer than 36 characters)
        msg = 'Max partition name length is 36 characters'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            part.SetName('a' * 37, self.no_options,
                         dbus_interface=self.iface_prefix + '.Partition')

        # set part name
        part.SetName('test', self.no_options,
                     dbus_interface=self.iface_prefix + '.Partition')

        self.udev_settle()

        # test flags value on types
        dbus_name = self.get_property(part, '.Partition', 'Name')
        dbus_name.assertEqual('test')

        # test flags value from sysytem
        part_name = str(part.object_path).split('/')[-1]
        _ret, sys_name = self.run_command('lsblk -d -no PARTLABEL /dev/%s' % part_name)
        self.assertEqual(sys_name, 'test')
