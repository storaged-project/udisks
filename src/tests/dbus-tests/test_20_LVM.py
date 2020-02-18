import dbus
import os
import re
import time
import unittest

from distutils.version import LooseVersion

import udiskstestcase

import gi
gi.require_version('BlockDev', '2.0')
from gi.repository import BlockDev


class UDisksLVMTestBase(udiskstestcase.UdisksTestCase):

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('LVM2'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for LVM tests not loaded, skipping.')

    @classmethod
    def _get_lvm_version(cls):
        _ret, out = cls.run_command('lvm version')
        m = re.search(r'LVM version:.* ([\d\.]+)', out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError('Failed to determine LVM version from: %s' % out)
        return LooseVersion(m.groups()[0])

    def _create_vg(self, vgname, devices):
        manager = self.get_object('/Manager')
        vg_path = manager.VolumeGroupCreate(vgname, devices, self.no_options,
                                            dbus_interface=self.iface_prefix + '.Manager.LVM2')
        vg = self.bus.get_object(self.iface_prefix, vg_path)
        self.assertIsNotNone(vg)
        # this makes sure the object is fully setup (e.g. has the Properties iface)
        vgsize = self.get_property(vg, '.VolumeGroup', 'Size')
        vgsize.assertGreater(0)
        ret, _out = self.run_command('vgs %s' % vgname)
        self.assertEqual(ret, 0)
        return vg

    def _remove_vg(self, vg):
        vgname = self.get_property_raw(vg, '.VolumeGroup', 'Name')
        vg.Delete(True, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup')
        ret, _out = self.run_command('vgs %s' % vgname)
        self.assertNotEqual(ret, 0)


class UdisksLVMTest(UDisksLVMTestBase):
    '''This is a basic LVM test suite'''

    def test_10_linear(self):
        '''Test linear (plain) LV functionality'''

        vgname = 'udisks_test_vg'

        # Use all the virtual devices but the last one
        devs = dbus.Array()
        for d in self.vdevs[:-1]:
            dev_obj = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)
        self.addCleanup(self._remove_vg, vg)

        dbus_vgname = self.get_property(vg, '.VolumeGroup', 'Name')
        dbus_vgname.assertEqual(vgname)

        # Create linear LV on the VG
        _ret, sys_vgsize = self.run_command('vgs -o size --noheadings --units=b --nosuffix %s' % vgname)
        vgsize = self.get_property(vg, '.VolumeGroup', 'Size')
        vgsize.assertEqual(int(sys_vgsize))

        _ret, sys_vgfree = self.run_command('vgs -o vg_free --noheadings --units=b --nosuffix %s' % vgname)
        vg_freesize = self.get_property(vg, '.VolumeGroup', 'FreeSize')
        vg_freesize.assertEqual(int(sys_vgfree))

        vg_freesize.assertEqual(vgsize.value)
        lvname = 'udisks_test_lv'
        lv_path = vg.CreatePlainVolume(lvname, dbus.UInt64(vgsize.value), self.no_options,
                                       dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.assertIsNotNone(lv_path)

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, lvname))
        self.assertEqual(ret, 0)

        lv = self.bus.get_object(self.iface_prefix, lv_path)
        lv_block_path = lv.Activate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.assertIsNotNone(lv_block_path)
        lvsize = self.get_property(lv, '.LogicalVolume', 'Size')
        lvsize.assertEqual(vgsize.value)

        # check some dbus properties
        dbus_vg = self.get_property(lv, '.LogicalVolume', 'VolumeGroup')
        dbus_vg.assertEqual(str(vg.object_path))

        dbus_name = self.get_property(lv, '.LogicalVolume', 'Name')
        dbus_name.assertEqual(lvname)

        dbus_active = self.get_property(lv, '.LogicalVolume', 'Active')
        dbus_active.assertTrue()

        dbus_type = self.get_property(lv, '.LogicalVolume', 'Type')
        dbus_type.assertEqual('block')  # type is only 'block' or 'pool'

        _ret, sys_uuid = self.run_command('lvs -o uuid --no-heading %s' % os.path.join(vgname, lvname))
        dbus_uuid = self.get_property(lv, '.LogicalVolume', 'UUID')
        dbus_uuid.assertEqual(sys_uuid)

        # check that the 'BlockDevice' property is set after Activate
        lv_prop_block = self.get_property(lv, '.LogicalVolume', 'BlockDevice')
        lv_prop_block.assertEqual(lv_block_path)

        # Shrink the LV
        lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        # check that the object is not on dbus and the 'BlockDevice' property is unset after Deactivate
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(lv_prop_block, objects.keys())

        lv_prop_block = self.get_property(lv, '.LogicalVolume', 'BlockDevice')
        lv_prop_block.assertEqual('/')

        dbus_active = self.get_property(lv, '.LogicalVolume', 'Active')
        dbus_active.assertFalse()

        lv.Resize(dbus.UInt64(lvsize.value/2), self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        lv_block_path = lv.Activate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        lv_block = self.bus.get_object(self.iface_prefix, lv_block_path)
        self.assertIsNotNone(lv_block)
        new_lvsize = self.get_property(lv, '.LogicalVolume', 'Size')
        new_lvsize.assertLess(lvsize.value)

        # check that the 'BlockDevice' property is set after Activate
        lv_prop_block = self.get_property(lv, '.LogicalVolume', 'BlockDevice')
        lv_prop_block.assertEqual(lv_block_path)

        # Add one more device to the VG
        new_dev_obj = self.get_object('/block_devices/' + os.path.basename(self.vdevs[-1]))
        self.assertIsNotNone(new_dev_obj)
        vg.AddDevice(new_dev_obj, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup')
        new_vgsize = self.get_property(vg, '.VolumeGroup', 'Size')
        new_vgsize.assertGreater(vgsize.value)

        # Resize the LV to the whole VG
        lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        lv.Resize(dbus.UInt64(new_vgsize.value), self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        new_lvsize = self.get_property(lv, '.LogicalVolume', 'Size')
        new_lvsize.assertEqual(new_vgsize.value)

        # rename the LV
        lvname = 'udisks_test_lv2'
        new_lvpath = lv.Rename(lvname, self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        # get the new (renamed) lv object
        lv = self.bus.get_object(self.iface_prefix, new_lvpath)
        self.assertIsNotNone(lv)

        dbus_name = self.get_property(lv, '.LogicalVolume', 'Name')
        dbus_name.assertEqual(lvname)

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, lvname))
        self.assertEqual(ret, 0)

        # lvremove
        lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        lv.Delete(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, lvname))
        self.assertNotEqual(ret, 0)

        # make sure the lv is not on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(new_lvpath, objects.keys())

    def test_20_thin(self):
        '''Test thin volumes functionality'''

        vgname = 'udisks_test_thin_vg'

        # Use all the virtual devices
        devs = dbus.Array()
        for d in self.vdevs:
            dev_obj = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)
        self.addCleanup(self._remove_vg, vg)

        # Create thin pool on the VG
        vgsize = int(self.get_property_raw(vg, '.VolumeGroup', 'FreeSize'))
        tpname = 'udisks_test_tp'
        tp_path = vg.CreateThinPoolVolume(tpname, dbus.UInt64(vgsize), self.no_options,
                                          dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.assertIsNotNone(tp_path)

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, tpname))
        self.assertEqual(ret, 0)

        tp = self.bus.get_object(self.iface_prefix, tp_path)
        tpsize = self.get_property(tp, '.LogicalVolume', 'Size')

        # check that we report same size as lvs (udisks includes metadata so we need to add it too)
        _ret, dsize = self.run_command('lvs -olv_size --noheadings --units=b --nosuffix %s' % os.path.join(vgname, tpname))
        _ret, msize = self.run_command('lvs -olv_metadata_size --noheadings --units=b --nosuffix %s' % os.path.join(vgname, tpname))
        tpsize.assertEqual(int(dsize.strip()) + int(msize.strip()))

        dbus_type = self.get_property(tp, '.LogicalVolume', 'Type')
        dbus_type.assertEqual("pool")

        # Create thin volume in the pool with virtual size twice the backing pool
        tvname = 'udisks_test_tv'
        tv_path = vg.CreateThinVolume(tvname, dbus.UInt64(int(tpsize.value) * 2), tp, self.no_options,
                                      dbus_interface=self.iface_prefix + '.VolumeGroup')
        tv = self.bus.get_object(self.iface_prefix, tv_path)
        self.assertIsNotNone(tv)

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, tvname))
        self.assertEqual(ret, 0)

        # Check the block device of the thin volume
        lv_block_path = tv.Activate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        lv_block = self.bus.get_object(self.iface_prefix, lv_block_path)
        self.assertIsNotNone(lv_block)
        blocksize = self.get_property(lv_block, '.Block', 'Size')
        blocksize.assertGreater(vgsize)

        tv_tp = self.get_property(tv, '.LogicalVolume', 'ThinPool')
        tv_tp.assertEqual(tp_path)

    def test_30_snapshot(self):
        '''Test LVM snapshoting'''

        vgname = 'udisks_test_snap_vg'

        # Use all the virtual devices
        devs = dbus.Array()
        for d in self.vdevs:
            dev_obj = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)
        self.addCleanup(self._remove_vg, vg)

        # Create the origin LV
        vgsize = int(self.get_property_raw(vg, '.VolumeGroup', 'FreeSize'))
        lvname = 'udisks_test_origin_lv'
        lv_path = vg.CreatePlainVolume(lvname, dbus.UInt64(vgsize / 2), self.no_options,
                                       dbus_interface=self.iface_prefix + '.VolumeGroup')
        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, lvname))
        self.assertEqual(ret, 0)

        # Create the LV's snapshot
        snapname = 'udisks_test_snap_lv'
        vg_freesize = int(self.get_property_raw(vg, '.VolumeGroup', 'FreeSize'))
        snap_path = lv.CreateSnapshot(snapname, vg_freesize, self.no_options,
                                      dbus_interface=self.iface_prefix + '.LogicalVolume')
        snap = self.bus.get_object(self.iface_prefix, snap_path)
        self.assertIsNotNone(snap)

        # check dbus properties
        dbus_origin = self.get_property(snap, '.LogicalVolume', 'Origin')
        dbus_origin.assertEqual(lv_path)

        dbus_name = self.get_property(snap, '.LogicalVolume', 'Name')
        dbus_name.assertEqual(snapname)

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, snapname))
        self.assertEqual(ret, 0)

    def test_40_cache(self):
        '''Basic LVM cache test'''

        vgname = 'udisks_test_cache_vg'

        # Use all the virtual devices
        devs = dbus.Array()
        for d in self.vdevs:
            dev_obj = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)
        self.addCleanup(self._remove_vg, vg)

        # Create the origin LV
        vgsize = int(self.get_property_raw(vg, '.VolumeGroup', 'FreeSize'))
        orig_lvname = 'udisks_test_origin_lv'
        lv_path = vg.CreatePlainVolume(orig_lvname, dbus.UInt64(vgsize / 2), self.no_options,
                                       dbus_interface=self.iface_prefix + '.VolumeGroup')
        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, orig_lvname))
        self.assertEqual(ret, 0)

        # Create the caching LV
        cache_lvname = 'udisks_test_cache_lv'
        vgsize = int(self.get_property_raw(vg, '.VolumeGroup', 'FreeSize'))
        # 8 MiB reserved for the cache metadata created automatically by LVM
        lv_cache_path = vg.CreatePlainVolume(cache_lvname, dbus.UInt64((vgsize / 2) - 8 * 1024**2), self.no_options,
                                             dbus_interface=self.iface_prefix + '.VolumeGroup')
        cache_lv = self.bus.get_object(self.iface_prefix, lv_cache_path)
        self.assertIsNotNone(cache_lv)

        # Add the cache to the origin
        lv.CacheAttach('udisks_test_cache_lv', self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        _ret, out = self.run_command('lvs %s/%s --noheadings -o segtype' % (vgname, orig_lvname))
        self.assertEqual(out, 'cache')

        # Split the cache
        lv.CacheSplit(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        _ret, out = self.run_command('lvs %s/%s --noheadings -o lv_layout' % (vgname, orig_lvname))
        self.assertEqual(out, 'linear')

        _ret, out = self.run_command('lvs %s/%s --noheadings -o lv_layout' % (vgname, cache_lvname))
        self.assertEqual(out, 'cache,pool')

    def test_50_rename_vg(self):
        ''' Test VG renaming '''

        vgname = 'udisks_test_rename_vg'

        # Use all the virtual devices
        devs = dbus.Array()
        for d in self.vdevs:
            dev_obj = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)

        vg = self._create_vg(vgname, devs)

        vgname = 'udisks_test_rename_vg2'
        new_vgpath = vg.Rename(vgname, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup')

        # get the new (renamed) lv object
        vg = self.bus.get_object(self.iface_prefix, new_vgpath)
        self.assertIsNotNone(vg)
        self.addCleanup(self._remove_vg, vg)

        dbus_name = self.get_property(vg, '.VolumeGroup', 'Name')
        dbus_name.assertEqual(vgname)

        ret, _out = self.run_command('vgs %s' % vgname)
        self.assertEqual(ret, 0)

    def test_60_pvs(self):
        ''' Test adding and removing PVs from VG '''

        vgname = 'udisks_test_pv_vg'

        # crete vg with one pv
        old_pv = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(old_pv)

        vg = self._create_vg(vgname, dbus.Array([old_pv]))
        self.addCleanup(self._remove_vg, vg)

        # create an lv on it
        lvname = 'udisks_test_lv'
        lv_path = vg.CreatePlainVolume(lvname, dbus.UInt64(4 * 1024**2), self.no_options,
                                       dbus_interface=self.iface_prefix + '.VolumeGroup')
        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)

        # add a new pv to the vg
        new_pv = self.get_object('/block_devices/' + os.path.basename(self.vdevs[1]))
        vg.AddDevice(new_pv, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup')

        _ret, out = self.run_command('pvs --noheadings -o vg_name %s' % self.vdevs[1])
        self.assertEqual(out, vgname)

        # empty the old pv
        vg.EmptyDevice(old_pv, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup', timeout=120 * 100)

        _ret, pv_size = self.run_command('pvs --noheadings --units=B --nosuffix -o pv_size %s' % self.vdevs[0])
        _ret, pv_free = self.run_command('pvs --noheadings --units=B --nosuffix -o pv_free %s' % self.vdevs[0])
        self.assertEqual(pv_size, pv_free)

        # remove the old pv from the vg
        vg.RemoveDevice(old_pv, False, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup')

        _ret, out = self.run_command('pvs --noheadings -o vg_name %s' % self.vdevs[0])
        self.assertEqual(out, '')


class UdisksLVMVDOTest(UDisksLVMTestBase):
    '''This is a basic LVM VDO test suite'''

    LOOP_DEVICE_PATH = '/var/tmp/udisks_test_disk_lvmvdo'

    @classmethod
    def setUpClass(cls):
        UDisksLVMTestBase.setUpClass()

        if not BlockDev.utils_have_kernel_module('kvdo'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('VDO kernel module not available, skipping.')

        lvm_version = cls._get_lvm_version()
        if lvm_version < LooseVersion('2.3.07'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('LVM >= 2.3.07 is needed for LVM VDO, skipping.')

    def setUp(self):
        # create backing sparse file
        # VDO needs at least 5G of space and we need some room for the grow test
        # ...rumors go that vdo internally operates on 2G extents...
        self.run_command('truncate -s 8G %s' % self.LOOP_DEVICE_PATH)
        ret_code, self.dev_name = self.run_command('losetup --find --show %s' % self.LOOP_DEVICE_PATH)
        self.assertEqual(ret_code, 0)
        time.sleep(0.5)
        self.device = self.get_device(self.dev_name)
        self.assertIsNotNone(self.device)
        super(UdisksLVMVDOTest, self).setUp()

    def tearDown(self):
        # need to process scheduled cleanup before the backing device is torn down
        self.doCleanups()
        # tear down loop device
        self.run_command('losetup --detach %s' % self.dev_name)
        os.remove(self.LOOP_DEVICE_PATH)
        super(UdisksLVMVDOTest, self).tearDown()

    def test_create(self):
        vgname = 'udisks_test_vdo_vg'

        # create vg on our testing device
        vg = self._create_vg(vgname, [self.device])
        self.addCleanup(self._remove_vg, vg)

        vg_free = self.get_property(vg, '.VolumeGroup', 'FreeSize')
        lv_name = 'udisks_test_vdovlv'
        pool_name = 'udisks_test_vdopool'
        psize = vg_free.value
        vsize = psize * 5
        lv_path = vg.CreateVDOVolume(lv_name, pool_name, dbus.UInt64(psize), dbus.UInt64(vsize),
                                     dbus.UInt64(0), True, True, "auto", self.no_options,
                                     dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.assertIsNotNone(lv_path)

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, lv_name))
        self.assertEqual(ret, 0)

        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)
        self.assertHasIface(lv, self.iface_prefix + '.VDOVolume')

        dbus_name = self.get_property(lv, '.LogicalVolume', 'Name')
        dbus_name.assertEqual(lv_name)

        # lv size -> original 'virtual' size
        dbus_size = self.get_property(lv, '.LogicalVolume', 'Size')
        dbus_size.assertEqual(vsize)

        # VDO pool properties
        pool_path = self.get_property(lv, '.VDOVolume', 'VDOPool')
        pool_path.assertNotEqual('/')
        pool = self.bus.get_object(self.iface_prefix, pool_path.value)
        self.assertIsNotNone(pool)

        dbus_name = self.get_property(pool, '.LogicalVolume', 'Name')
        dbus_name.assertEqual(pool_name)

        # pool size -> original 'physical' size
        dbus_size = self.get_property(pool, '.LogicalVolume', 'Size')
        dbus_size.assertEqual(psize)

        # VDO properties
        dbus_comp = self.get_property(lv, '.VDOVolume', 'Compression')
        dbus_comp.assertTrue()

        dbus_dedup = self.get_property(lv, '.VDOVolume', 'Deduplication')
        dbus_dedup.assertTrue()

        # ThinPool property should not be set
        dbus_tp = self.get_property(lv, '.LogicalVolume', 'ThinPool')
        dbus_tp.assertEqual('/')

        # get statistics and do some simple sanity check
        stats = lv.GetStatistics(self.no_options, dbus_interface=self.iface_prefix + '.VDOVolume')
        self.assertIn("writeAmplificationRatio", stats.keys())
        self.assertGreater(float(stats["writeAmplificationRatio"]), 0)

    def test_enable_disable_compression_deduplication(self):
        vgname = 'udisks_test_vdo_vg'

        # create vg on our testing device
        vg = self._create_vg(vgname, [self.device])
        self.addCleanup(self._remove_vg, vg)

        vg_free = self.get_property(vg, '.VolumeGroup', 'FreeSize')
        lv_name = 'udisks_test_vdovlv'
        pool_name = 'udisks_test_vdopool'
        psize = vg_free.value
        vsize = psize * 5
        lv_path = vg.CreateVDOVolume(lv_name, pool_name, dbus.UInt64(psize), dbus.UInt64(vsize),
                                     dbus.UInt64(0), True, True, "auto", self.no_options,
                                     dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.assertIsNotNone(lv_path)

        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)

        # initial state: both compression and deduplication should be enabled
        dbus_comp = self.get_property(lv, '.VDOVolume', 'Compression')
        dbus_comp.assertTrue()

        dbus_dedup = self.get_property(lv, '.VDOVolume', 'Deduplication')
        dbus_dedup.assertTrue()

        # disable deduplication
        lv.EnableDeduplication(False, self.no_options,
                               dbus_interface=self.iface_prefix + '.VDOVolume')
        dbus_dedup = self.get_property(lv, '.VDOVolume', 'Deduplication')
        dbus_dedup.assertFalse()

        # disable compression
        lv.EnableCompression(False, self.no_options,
                             dbus_interface=self.iface_prefix + '.VDOVolume')
        dbus_comp = self.get_property(lv, '.VDOVolume', 'Compression')
        dbus_comp.assertFalse()

        # enable both again
        lv.EnableDeduplication(True, self.no_options,
                               dbus_interface=self.iface_prefix + '.VDOVolume')
        dbus_dedup = self.get_property(lv, '.VDOVolume', 'Deduplication')
        dbus_dedup.assertTrue()

        # disable compression
        lv.EnableCompression(True, self.no_options,
                             dbus_interface=self.iface_prefix + '.VDOVolume')
        dbus_comp = self.get_property(lv, '.VDOVolume', 'Compression')
        dbus_comp.assertTrue()

    def test_resize_logical(self):
        vgname = 'udisks_test_vdo_vg'

        # create vg on our testing device
        vg = self._create_vg(vgname, [self.device])
        self.addCleanup(self._remove_vg, vg)

        vg_free = self.get_property(vg, '.VolumeGroup', 'FreeSize')
        lv_name = 'udisks_test_vdovlv'
        pool_name = 'udisks_test_vdopool'
        psize = vg_free.value
        vsize = psize
        lv_path = vg.CreateVDOVolume(lv_name, pool_name, dbus.UInt64(psize), dbus.UInt64(vsize),
                                     dbus.UInt64(0), True, True, "auto", self.no_options,
                                     dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.assertIsNotNone(lv_path)

        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)

        lv.ResizeLogical(vsize * 5, self.no_options,
                         dbus_interface=self.iface_prefix + '.VDOVolume')
        dbus_size = self.get_property(lv, '.LogicalVolume', 'Size')
        dbus_size.assertEqual(vsize * 5)

    def test_resize_physical(self):
        vgname = 'udisks_test_vdo_vg'

        # create vg on our testing device
        vg = self._create_vg(vgname, [self.device])
        self.addCleanup(self._remove_vg, vg)

        vg_free = self.get_property(vg, '.VolumeGroup', 'FreeSize')
        lv_name = 'udisks_test_vdovlv'
        pool_name = 'udisks_test_vdopool'
        psize = vg_free.value - 1 * 1024**3
        vsize = psize * 5
        lv_path = vg.CreateVDOVolume(lv_name, pool_name, dbus.UInt64(psize), dbus.UInt64(vsize),
                                     dbus.UInt64(0), True, True, "auto", self.no_options,
                                     dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.assertIsNotNone(lv_path)

        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.assertIsNotNone(lv)

        lv.ResizePhysical(vg_free.value, self.no_options,
                          dbus_interface=self.iface_prefix + '.VDOVolume')

        pool_path = self.get_property(lv, '.VDOVolume', 'VDOPool')
        pool_path.assertNotEqual('/')
        pool = self.bus.get_object(self.iface_prefix, pool_path.value)
        self.assertIsNotNone(pool)

        dbus_size = self.get_property(pool, '.LogicalVolume', 'Size')
        dbus_size.assertEqual(vg_free.value)
