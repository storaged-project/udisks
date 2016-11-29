import dbus
import os
import time

from collections import namedtuple
from contextlib import contextmanager

import storagedtestcase


Member = namedtuple('Member', ['obj', 'path', 'name', 'size'])


@contextmanager
def wait_for_action(action_name):
    try:
        yield
    finally:
        time.sleep(2)
        action = True
        while action:
            with open("/proc/mdstat", "r") as f:
                action = action_name in f.read()
            if action:
                time.sleep(1)


class RAIDLevel(storagedtestcase.StoragedTestCase):
    level = None
    min_members = 0
    members = None
    chunk_size = 0

    def setUp(self):
        if len(self.vdevs) < self.min_members:
            raise ValueError('Not enough members for %s' % self.level)

        self.members = []
        self.udev_settle()
        for dev in self.vdevs[:self.min_members]:
            dev_name = os.path.basename(dev)
            dev_obj = self.get_object('/block_devices/' + dev_name)
            self.assertIsNotNone(dev_obj)
            _ret, out = self.run_command('lsblk -b -no SIZE %s' % dev)  # get size of the device
            self.members.append(Member(obj=dev_obj, path=dev, name=dev_name, size=int(out)))

    @property
    def size(self):
        return 0

    @property
    def smallest_member(self):
        return min(self.members, key=lambda x: x.size)

    def _force_remove(self, array_name, array_members):
        # get path for the array (go through all possible members -- array might be incomplete)
        names = [self.run_command('ls /sys/block/%s/holders' % os.path.basename(m)) for m in array_members]
        md_name = next((name[1] for name in names if name[1]), None)  # get first non-empty name if exists

        # stop everything (twice)
        self.run_command('mdadm --stop %s' % array_name)

        if md_name:
            self.run_command('mdadm --stop /dev/%s' % md_name)

        # clear all members
        self.run_command('mdadm --zero-superblock --force %s' % ' '.join(array_members))

        # and stop again
        self.run_command('mdadm --stop %s' % array_name)

        if md_name:
            self.run_command('mdadm --stop /dev/%s' % md_name)

    def _zero_superblock(self, device):
        self.run_command('mdadm --zero-superblock --force %s' % device)

    def _array_create(self, array_name):
        # set the 'force' cleanup now in case create fails
        self.addCleanup(self._force_remove, array_name, [m.path for m in self.members])

        manager = self.get_object('/Manager')
        with wait_for_action('resync'):
            array_path = manager.MDRaidCreate(dbus.Array(m.obj for m in self.members),
                                              self.level, array_name, self.chunk_size,
                                              self.no_options,
                                              dbus_interface=self.iface_prefix + '.Manager')

        array = self.bus.get_object(self.iface_prefix, array_path)
        self.assertIsNotNone(array)
        return array

    def _md_data(self, array_name):
        _ret, out = self.run_command('mdadm --detail --export /dev/md/%s' % array_name)

        return {key: value for (key, value) in [line.split('=') for line in out.split()]}

    def test_create(self):
        if self.level is None:
            self.skipTest('Abstract class for RAID tests.')

        # create the array
        array_name = 'storaged_test_' + self.level
        array = self._array_create(array_name)

        # check if /dev/md/'name' exists
        self.assertTrue(os.path.exists('/dev/md/%s' % array_name))

        # get md_name ('md12X')
        md_name = os.path.realpath('/dev/md/%s' % array_name).split('/')[-1]

        # get 'system' data using mdadm
        md_data = self._md_data(array_name)

        # test dbus properties
        dbus_name = self.get_property(array, '.MDRaid', 'Name')

        # name of the array reported by 'mdadm' should look like 'nodename':'array_name'
        # if it's smaller than 31 characters
        nodename = os.uname().nodename
        if len(nodename + array_name) < 31:
            self.assertEqual(dbus_name, "%s:%s" % (nodename, array_name))
        else:
            self.assertEqual(dbus_name, array_name)

        self.assertEqual(dbus_name, md_data['MD_NAME'])

        dbus_level = self.get_property(array, '.MDRaid', 'Level')
        self.assertEqual(dbus_level, self.level)
        self.assertEqual(dbus_level, md_data['MD_LEVEL'])

        dbus_num_devices = self.get_property(array, '.MDRaid', 'NumDevices')
        self.assertEqual(dbus_num_devices, len(self.members))
        self.assertEqual(dbus_num_devices, int(md_data['MD_DEVICES']))

        dbus_uuid = self.get_property(array, '.MDRaid', 'UUID')
        self.assertEqual(dbus_uuid, md_data['MD_UUID'])

        dbus_chunk = self.get_property(array, '.MDRaid', 'ChunkSize')
        self.assertEqual(dbus_chunk, self.chunk_size)

        # check bitmap location
        dbus_bitmap = self.get_property(array, '.MDRaid', 'BitmapLocation')
        sys_bitmap = self.read_file('/sys/block/%s/md/bitmap/location' % md_name)

        # raid0 does not support write-intent bitmaps -> BitmapLocation is set to an empty string
        if self.level == 'raid0':
            self.assertEqual(self.ay_to_str(dbus_bitmap), '')
        else:
            self.assertEqual(self.ay_to_str(dbus_bitmap), sys_bitmap.strip())

        # check size of the array based on given disks, there will be some
        # difference because of metadata, but this should be less than 0.5 %
        dbus_size = self.get_property(array, '.MDRaid', 'Size')
        self.assertLessEqual(self.size - dbus_size, 0.005 * self.size)

        _ret, out = self.run_command('lsblk -b -no SIZE /dev/md/%s' % array_name)
        self.assertEqual(dbus_size, int(out))

        # test if mdadm see all members
        for member in self.members:
            self.assertIn('MD_DEVICE_%s_DEV' % member.name, md_data.keys())
            self.assertIn('MD_DEVICE_%s_ROLE' % member.name, md_data.keys())
            self.assertEqual(md_data['MD_DEVICE_%s_DEV' % member.name], member.path)

        # test if storaged see all (active) members
        dbus_devices = self.get_property(array, '.MDRaid', 'ActiveDevices')
        for dbus_dev in dbus_devices:
            # get matching member from self.members -- match using name and last part of dbus path
            member = next((m for m in self.members if m.name == dbus_dev[0].split('/')[-1]), None)
            self.assertIsNotNone(member)

            slot = int(md_data['MD_DEVICE_%s_ROLE' % member.name])
            self.assertEqual(dbus_dev[1], slot)

        # check if all members have 'MDRaidMember' set to object path of the array
        path = str(array.object_path)
        for member in self.members:
            dbus_member = self.get_property(member.obj, '.Block', 'MDRaidMember')
            self.assertEqual(dbus_member, path)


class RAID0TestCase(RAIDLevel):
    level = 'raid0'
    min_members = 2
    chunk_size = 4 * 1024  # 4 KiB

    @property
    def size(self):
        return len(self.members) * self.smallest_member.size

    def test_start_stop(self):
        name = 'storaged_test_start_stop'
        array = self._array_create(name)

        dbus_running = self.get_property(array, '.MDRaid', 'Running')
        self.assertTrue(dbus_running)

        # stop the array
        array.Stop(self.no_options, dbus_interface=self.iface_prefix + '.MDRaid')

        time.sleep(1)
        dbus_running = self.get_property(array, '.MDRaid', 'Running')
        self.assertFalse(dbus_running)

        ret, _out = self.run_command('mdadm /dev/md/%s' % name)
        self.assertEqual(ret, 1)

        # start the array
        array.Start(self.no_options, dbus_interface=self.iface_prefix + '.MDRaid')

        time.sleep(1)
        dbus_running = self.get_property(array, '.MDRaid', 'Running')
        self.assertTrue(dbus_running)

        ret, _out = self.run_command('mdadm /dev/md/%s' % name)
        self.assertEqual(ret, 0)

    def test_delete(self):
        name = 'storaged_test_delete'
        array = self._array_create(name)

        # stop the array
        array.Stop(self.no_options, dbus_interface=self.iface_prefix + '.MDRaid')
        ret, _out = self.run_command('mdadm /dev/md/%s' % name)
        self.assertEqual(ret, 1)

        # delete
        array.Delete(self.no_options, dbus_interface=self.iface_prefix + '.MDRaid')
        self.udev_settle()

        # make sure array is not on dbus
        path = str(array.object_path)
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(path, objects.keys())

        # check if members have been wiped and 'MDRaidMember' property is updated
        for member in self.members:
            _ret, out = self.run_command('lsblk -no FSTYPE %s' % member.path)
            self.assertEqual(out, '')

            dbus_member = self.get_property(member.obj, '.Block', 'MDRaidMember')
            self.assertEqual(dbus_member, '/')  # default value for 'non-raid' devices is '/'


class RAID1TestCase(RAIDLevel):
    level = 'raid1'
    min_members = 2
    chunk_size = 0  # 0 for raid1

    @property
    def size(self):
        return self.smallest_member.size

    def test_add_remove_device(self):
        name = 'storaged_delete'
        array = self._array_create(name)
        array_path = str(array.object_path)

        # device to add -- take the last vdev
        new_dev = self.vdevs[-1]
        new_name = os.path.basename(new_dev)
        new_obj = self.get_object('/block_devices/' + new_name)

        # make sure to clear this device
        self.addCleanup(self._zero_superblock, new_dev)

        self.assertIsNotNone(new_obj)
        new_path = str(new_obj.object_path)

        # add the device to the array
        array.AddDevice(new_path, self.no_options, dbus_interface=self.iface_prefix + '.MDRaid')
        self.udev_settle()

        # check ActiveDevices on dbus -- new device should be there with 'spare' status
        dbus_devices = self.get_property(array, '.MDRaid', 'ActiveDevices')
        member = next((d for d in dbus_devices if d[0].split('/')[-1] == new_name), None)
        self.assertIsNotNone(member)
        self.assertEqual(member[2], ['spare'])

        # check mdadm output
        md_data = self._md_data(name)

        self.assertIn('MD_DEVICE_%s_DEV' % new_name, md_data.keys())
        self.assertIn('MD_DEVICE_%s_ROLE' % new_name, md_data.keys())
        self.assertEqual(md_data['MD_DEVICE_%s_DEV' % new_name], new_dev)
        self.assertEqual(md_data['MD_DEVICE_%s_ROLE' % new_name], 'spare')

        # check if 'MDRaidMember' for new device is set
        dbus_member = self.get_property(new_obj, '.Block', 'MDRaidMember')
        self.assertEqual(dbus_member, array_path)

        # remove the device from the array
        d = dbus.Dictionary(signature='sv')
        d['wipe'] = True
        array.RemoveDevice(new_path, d, dbus_interface=self.iface_prefix + '.MDRaid')
        self.udev_settle()

        # check ActiveDevices on dbus -- new device shouldn't be there
        dbus_devices = self.get_property(array, '.MDRaid', 'ActiveDevices')
        member = next((d for d in dbus_devices if d[0].split('/')[-1] == new_name), None)
        self.assertIsNone(member)

        # check mdadm output
        md_data = self._md_data(name)

        self.assertNotIn('MD_DEVICE_%s_DEV' % new_name, md_data.keys())
        self.assertNotIn('MD_DEVICE_%s_ROLE' % new_name, md_data.keys())

        # check if 'MDRaidMember' for new device is unset after remove
        dbus_member = self.get_property(new_obj, '.Block', 'MDRaidMember')
        self.assertEqual(dbus_member, '/')  # default value for 'non-raid' devices is '/'

        # with 'wipe' option, device should be wiped
        _ret, out = self.run_command('lsblk -no FSTYPE %s' % new_path)
        self.assertEqual(out, '')

    def test_bitmap_location(self):
        array_name = 'storaged_test_bitmap'
        array = self._array_create(array_name)

        # get md_name ('/dev/md12X')
        md_name = os.path.realpath('/dev/md/%s' % array_name).split('/')[-1]

        # change bitmap location to 'internal'
        loc = self.str_to_ay('internal')

        array.SetBitmapLocation(loc, self.no_options, dbus_interface=self.iface_prefix + '.MDRaid')
        time.sleep(1)

        dbus_bitmap = self.get_property(array, '.MDRaid', 'BitmapLocation')
        sys_bitmap = self.read_file('/sys/block/%s/md/bitmap/location' % md_name).strip()
        self.assertEqual(sys_bitmap, self.ay_to_str(dbus_bitmap))

        # change bitmap location back to 'none'
        loc = self.str_to_ay('none')

        array.SetBitmapLocation(loc, self.no_options, dbus_interface=self.iface_prefix + '.MDRaid')
        time.sleep(1)

        dbus_bitmap = self.get_property(array, '.MDRaid', 'BitmapLocation')
        sys_bitmap = self.read_file('/sys/block/%s/md/bitmap/location' % md_name).strip()
        self.assertEqual(sys_bitmap, self.ay_to_str(dbus_bitmap))

    def test_request_action(self):

        array_name = 'storaged_test_request'
        array = self._array_create(array_name)

        # get md_name ('/dev/md12X')
        md_name = os.path.realpath('/dev/md/%s' % array_name).split('/')[-1]

        # request check and wait till ended
        with wait_for_action('check'):
            array.RequestSyncAction('check', self.no_options, dbus_interface=self.iface_prefix + '.MDRaid')

        # last sync action should be check
        sys_action = self.read_file('/sys/block/%s/md/last_sync_action' % md_name).strip()
        self.assertEqual(sys_action, 'check')


class RAID4TestCase(RAIDLevel):
    level = 'raid4'
    min_members = 3
    chunk_size = 4 * 1024  # 4 KiB

    @property
    def size(self):
        return self.smallest_member.size * (len(self.members) - 1)


class RAID5TestCase(RAIDLevel):
    level = 'raid5'
    min_members = 3
    chunk_size = 4 * 1024  # 4 KiB

    @property
    def size(self):
        return self.smallest_member.size * (len(self.members) - 1)


class RAID6TestCase(RAIDLevel):
    level = 'raid6'
    min_members = 4
    chunk_size = 4 * 1024  # 4 KiB

    @property
    def size(self):
        return self.smallest_member.size * (len(self.members) - 2)


class RAID10TestCase(RAIDLevel):
    level = 'raid10'
    min_members = 4
    chunk_size = 4 * 1024  # 4 KiB

    @property
    def size(self):
        return self.smallest_member.size * (len(self.members) // 2)


del RAIDLevel  # skip RAIDLevel
