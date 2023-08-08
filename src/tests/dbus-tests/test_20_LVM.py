import dbus
import os
import re
import time
import unittest
import six
import sys
import glob

from packaging.version import Version

import udiskstestcase


class UDisksLVMTestBase(udiskstestcase.UdisksTestCase):

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('lvm2'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for LVM tests not loaded, skipping.')

    @classmethod
    def _get_lvm_version(cls):
        _ret, out = cls.run_command('lvm version')
        m = re.search(r'LVM version:.* ([\d\.]+)', out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError('Failed to determine LVM version from: %s' % out)
        return Version(m.groups()[0])

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

    def _remove_vg(self, vg, tear_down=False, ignore_removed=False):
        try:
            vgname = self.get_property_raw(vg, '.VolumeGroup', 'Name')
            if tear_down:
                options = dbus.Dictionary(signature='sv')
                options['tear-down'] = dbus.Boolean(True)
            else:
                options = self.no_options
            vg.Delete(True, options, dbus_interface=self.iface_prefix + '.VolumeGroup')
            ret, _out = self.run_command('vgs %s' % vgname)
            self.assertNotEqual(ret, 0)
        except dbus.exceptions.DBusException as e:
            if not ignore_removed:
                raise e


class UdisksLVMTest(UDisksLVMTestBase):
    '''This is a basic LVM test suite'''

    def _rescan_lio_devices(self):
        ''' Bring back all vdevs that have been deleted by the test '''
        ret, out = self.run_command("for f in $(find /sys/devices -path '*tcm_loop*/scan'); do echo '- - -' >$f; done")
        if ret != 0:
            raise RuntimeError("Cannot rescan vdevs: %s", out)
        self.udev_settle()
        # device names might have changed, need to find our vdevs again
        tcmdevs = glob.glob('/sys/devices/*tcm_loop*/tcm_loop_adapter_*/*/*/*/block/sd*')
        udiskstestcase.test_devs = self.vdevs = ['/dev/%s' % os.path.basename(p) for p in tcmdevs]
        for d in self.vdevs:
            obj = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertHasIface(obj, self.iface_prefix + '.Block')

    def test_01_manager_interface(self):
        '''Test for module D-Bus Manager interface presence'''

        manager = self.get_object('/Manager')
        intro_data = manager.Introspect(self.no_options, dbus_interface='org.freedesktop.DBus.Introspectable')
        self.assertIn('interface name="%s.Manager.LVM2"' % self.iface_prefix, intro_data)

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

        dbus_layout = self.get_property(lv, '.LogicalVolume', 'Layout')
        dbus_layout.assertEqual('linear')

        def assertSegs(pvs):
            # Check that there is exactly one segment per PV
            struct = self.get_property(lv, '.LogicalVolume', 'Structure').value
            self.assertEqual(struct["type"], "linear")
            self.assertNotIn("data", struct)
            self.assertNotIn("metadata", struct)
            segs = struct["segments"]
            self.assertEqual(len(segs), len(pvs))
            seg_pvs = list(map(lambda s: s[2], segs))
            for p in pvs:
                self.assertIn(p.object_path, seg_pvs)

        assertSegs(devs)

        _ret, sys_uuid = self.run_command('lvs -o uuid --no-heading %s' % os.path.join(vgname, lvname))
        dbus_uuid = self.get_property(lv, '.LogicalVolume', 'UUID')
        dbus_uuid.assertEqual(sys_uuid)

        # check that the 'BlockDevice' property is set after Activate
        lv_prop_block = self.get_property(lv, '.LogicalVolume', 'BlockDevice')
        lv_prop_block.assertEqual(lv_block_path)

        # Shrink the LV
        lv.Resize(dbus.UInt64(lvsize.value/2), self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        lv_block = self.bus.get_object(self.iface_prefix, lv_block_path)
        self.assertIsNotNone(lv_block)
        new_lvsize = self.get_property(lv, '.LogicalVolume', 'Size')
        new_lvsize.assertLess(lvsize.value)

        # Add one more device to the VG
        new_dev_obj = self.get_object('/block_devices/' + os.path.basename(self.vdevs[-1]))
        self.assertIsNotNone(new_dev_obj)
        vg.AddDevice(new_dev_obj, self.no_options, dbus_interface=self.iface_prefix + '.VolumeGroup')
        new_vgsize = self.get_property(vg, '.VolumeGroup', 'Size')
        new_vgsize.assertGreater(vgsize.value)

        # Attempt to resize the LV to the whole VG, but specify only
        # the original PVS.  This is expected to fail.
        msg = "Insufficient free space"
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            lv.Resize(dbus.UInt64(new_vgsize.value),
                      dbus.Dictionary({'pvs': devs}, signature='sv'),
                      dbus_interface=self.iface_prefix + '.LogicalVolume')

        # Now resize the LV to the whole VG without contraints
        lv.Resize(dbus.UInt64(new_vgsize.value), self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        new_lvsize = self.get_property(lv, '.LogicalVolume', 'Size')
        new_lvsize.assertEqual(new_vgsize.value)

        assertSegs(devs + [ new_dev_obj ])

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

        # deactivate/activate check
        dbus_prop_active = self.get_property(lv, '.LogicalVolume', 'Active')
        dbus_prop_active.assertTrue()
        ret, _out = self.run_command('lvchange %s --activate n' % os.path.join(vgname, lvname))
        self.assertEqual(ret, 0)
        time.sleep(3)
        dbus_prop_active = self.get_property(lv, '.LogicalVolume', 'Active')
        dbus_prop_active.assertFalse()
        ret, _out = self.run_command('lvchange %s --activate y' % os.path.join(vgname, lvname))
        self.assertEqual(ret, 0)
        time.sleep(3)
        dbus_prop_active = self.get_property(lv, '.LogicalVolume', 'Active')
        dbus_prop_active.assertTrue()
        lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        dbus_prop_active = self.get_property(lv, '.LogicalVolume', 'Active')
        dbus_prop_active.assertFalse()

        # lvremove
        lv.Delete(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        ret, _out = self.run_command('lvs %s' % os.path.join(vgname, lvname))
        self.assertNotEqual(ret, 0)

        # make sure the lv is not on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(new_lvpath, objects.keys())

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_15_raid(self):
        '''Test raid volumes functionality'''

        vgname = 'udisks_test_vg'

        # Use all the virtual devices
        devs = dbus.Array()
        for d in self.vdevs:
            dev_obj = self.get_object('/block_devices/' + os.path.basename(d))
            self.assertIsNotNone(dev_obj)
            devs.append(dev_obj)
        vg = self._create_vg(vgname, devs)
        self.addCleanup(self._remove_vg, vg)

        dbus_vgname = self.get_property(vg, '.VolumeGroup', 'Name')
        dbus_vgname.assertEqual(vgname)

        first_vdev_uuid = self.get_property(devs[0], '.Block', 'IdUUID').value

        # Create raid1 LV on the VG
        lvname = 'udisks_test_lv'
        vg_freesize = self.get_property(vg, '.VolumeGroup', 'FreeSize')
        vdev_size = vg_freesize.value / len(devs)
        lv_size = int(vdev_size * 0.75)
        lv_path = vg.CreatePlainVolumeWithLayout(lvname, dbus.UInt64(lv_size),
                                                 "raid1", devs[0:3],
                                                 self.no_options,
                                                 dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.assertIsNotNone(lv_path)

        lv = self.bus.get_object(self.iface_prefix, lv_path)
        self.get_property(lv, '.LogicalVolume', 'SyncRatio').assertEqual(1.0, timeout=60, poll_vg=vg)

        _ret, sys_type = self.run_command('lvs -o seg_type --noheadings --nosuffix %s/%s' % (vgname, lvname))
        self.assertEqual(sys_type, "raid1")
        self.get_property(lv, '.LogicalVolume', 'Layout').assertEqual("raid1")

        def assertSegs(struct, size, pv):
            self.assertEqual(struct["type"], "linear")
            self.assertNotIn("data", struct)
            self.assertNotIn("metadata", struct)
            if pv is not None:
                self.assertEqual(len(struct["segments"]), 1)
                if size is not None:
                    self.assertEqual(struct["segments"][0][1], size)
                self.assertEqual(struct["segments"][0][2], pv.object_path)
            else:
                self.assertEqual(len(struct["segments"]), 0)

        def assertRaid1Stripes(structs, size, pv1, pv2, pv3):
            self.assertEqual(len(structs), 3)
            assertSegs(structs[0], size, pv1)
            assertSegs(structs[1], size, pv2)
            assertSegs(structs[2], size, pv3)

        def assertRaid1Structure(pv1, pv2, pv3):
            struct = self.get_property(lv, '.LogicalVolume', 'Structure').value
            self.assertEqual(struct["type"], "raid1")
            self.assertEqual(struct["size"], lv_size)
            self.assertNotIn("segments", struct)
            assertRaid1Stripes(struct["data"], lv_size, pv1, pv2, pv3)
            assertRaid1Stripes(struct["metadata"], None, pv1, pv2, pv3)

        def waitRaid1Structure(pv1, pv2, pv3):
            for _ in range(5):
                try:
                    assertRaid1Structure(pv1, pv2, pv3)
                    return
                except AssertionError:
                    pass
                time.sleep(1)
            # Once again for the error message
            assertRaid1Structure(pv1, pv2, pv3)

        waitRaid1Structure(devs[0], devs[1], devs[2])

        # Yank out the first vdev and repair the LV with the fourth
        _ret, _output = self.run_command('echo yes >/sys/block/%s/device/delete' % os.path.basename(self.vdevs[0]))
        self.addCleanup(self._rescan_lio_devices)
        self.get_property(vg, '.VolumeGroup', 'MissingPhysicalVolumes').assertEqual([first_vdev_uuid])
        _ret, sys_health = self.run_command('lvs -o health_status --noheadings --nosuffix %s/%s' % (vgname, lvname))
        self.assertEqual(sys_health, "partial")

        waitRaid1Structure(None, devs[1], devs[2])

        lv.Repair(devs[3:4], self.no_options,
                  dbus_interface=self.iface_prefix + '.LogicalVolume')
        _ret, sys_health = self.run_command('lvs -o health_status --noheadings --nosuffix %s/%s' % (vgname, lvname))
        self.assertEqual(sys_health, "")
        self.get_property(lv, '.LogicalVolume', 'SyncRatio').assertEqual(1.0, timeout=60, poll_vg=vg)

        waitRaid1Structure(devs[3], devs[1], devs[2])

        # Tell the VG that everything is alright
        vg.RemoveMissingPhysicalVolumes(self.no_options,
                                        dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.get_property(vg, '.VolumeGroup', 'MissingPhysicalVolumes').assertEqual([])

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

        # create vg with one pv
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

        if not cls.module_available("kvdo"):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('VDO kernel module not available, skipping.')

        lvm_version = cls._get_lvm_version()
        if lvm_version < Version('2.3.07'):
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
        vg_free.assertGreater(0)
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

        dbus_type = self.get_property(lv, '.LogicalVolume', 'Type')
        dbus_type.assertNotEqual('vdopool')

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

    def test_enable_disable_compression_deduplication(self):
        vgname = 'udisks_test_vdo_vg'

        # create vg on our testing device
        vg = self._create_vg(vgname, [self.device])
        self.addCleanup(self._remove_vg, vg)

        vg_free = self.get_property(vg, '.VolumeGroup', 'FreeSize')
        vg_free.assertGreater(0)
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
        vg_free.assertGreater(0)
        lv_name = 'udisks_test_vdovlv'
        pool_name = 'udisks_test_vdopool'
        psize = vg_free.value
        vsize = psize * 2
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

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_resize_physical(self):
        vgname = 'udisks_test_vdo_vg'

        # create vg on our testing device
        vg = self._create_vg(vgname, [self.device])
        self.addCleanup(self._remove_vg, vg)

        vg_free = self.get_property(vg, '.VolumeGroup', 'FreeSize')
        vg_free.assertGreater(2 * 1024**3)
        lv_name = 'udisks_test_vdovlv'
        pool_name = 'udisks_test_vdopool'
        psize = vg_free.value - 2 * 1024**3
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


class UdisksLVMTeardownTest(UDisksLVMTestBase):
    '''Stacked LVM + LUKS automatic teardown tests'''

    PASSPHRASE = 'einszweidrei'

    def setUp(self):
        super(UdisksLVMTeardownTest, self).setUp()

    def tearDown(self):
        self.doCleanups()
        super(UdisksLVMTeardownTest, self).tearDown()

    def _remove_luks(self, device, name, close=True):
        if close:
            try:
                self.remove_file('/etc/luks-keys/%s' % name, ignore_nonexistent=True)
                device.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
            except dbus.exceptions.DBusException as e:
                # ignore when luks is actually already locked
                if not str(e).endswith('is not unlocked') and not 'No such interface' in str(e) and \
                   not 'Object does not exist at path' in str(e):
                    raise e

        try:
            d = dbus.Dictionary(signature='sv')
            d['erase'] = True
            device.Format('empty', d, dbus_interface=self.iface_prefix + '.Block')
        except dbus.exceptions.DBusException as e:
            if not 'No such interface' in str(e) and not 'Object does not exist at path' in str(e):
                raise e

    def _init_stack(self, name):
        vgname = name + '_vg'
        lvname = name + '_lv'

        # backup and restore
        crypttab = self.read_file('/etc/crypttab')
        self.addCleanup(self.write_file, '/etc/crypttab', crypttab)
        fstab = self.read_file('/etc/fstab')
        self.addCleanup(self.write_file, '/etc/fstab', fstab)

        # create VG with one PV
        self.pv = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(self.pv)

        self.vg = self._create_vg(vgname, dbus.Array([self.pv]))
        self.vg_path = self.vg.object_path
        self.addCleanup(self._remove_vg, self.vg, tear_down=True, ignore_removed=True)

        # create an LV on it
        self.lv_path = self.vg.CreatePlainVolume(lvname, dbus.UInt64(200 * 1024**2), self.no_options,
                                                 dbus_interface=self.iface_prefix + '.VolumeGroup')
        self.lv = self.bus.get_object(self.iface_prefix, self.lv_path)
        self.assertIsNotNone(self.lv)

        self.lv_block_path = self.lv.Activate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self.assertIsNotNone(self.lv_block_path)

        self.lv_block = self.get_object(self.lv_block_path)
        self.assertIsNotNone(self.lv_block)

        # create LUKS on the LV
        options = dbus.Dictionary(signature='sv')
        options['encrypt.type'] = 'luks2'
        options['encrypt.passphrase'] = self.PASSPHRASE
        options['label'] = 'COCKPITFS'
        options['tear-down'] = dbus.Boolean(True)

        crypttab_items = dbus.Dictionary({'name': self.str_to_ay(vgname),
                                          'options': self.str_to_ay('verify,discard'),
                                          'passphrase-contents': self.str_to_ay(self.PASSPHRASE),
                                          'track-parents': True},
                                          signature=dbus.Signature('sv'))
        fstab_items = dbus.Dictionary({'dir': self.str_to_ay(vgname),
                                       'type': self.str_to_ay('ext4'),
                                       'opts': self.str_to_ay('defaults'),
                                       'freq': 0, 'passno': 0,
                                       'track-parents': True},
                                      signature=dbus.Signature('sv'))
        options['config-items'] = dbus.Array([('crypttab', crypttab_items),
                                              ('fstab', fstab_items)])

        self.lv_block.Format('ext4', options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self._remove_luks, self.lv_block, vgname)
        self.luks_uuid = self.get_property_raw(self.lv_block, '.Block', 'IdUUID')

        self.luks_block_path = self.get_property_raw(self.lv_block, '.Encrypted', 'CleartextDevice')
        luks_block = self.get_object(self.luks_block_path)
        self.assertIsNotNone(luks_block)
        self.fs_uuid = self.get_property_raw(luks_block, '.Block', 'IdUUID')

        # check for present crypttab configuration item
        conf = self.get_property(self.lv_block, '.Block', 'Configuration')
        conf.assertTrue()
        self.assertEqual(conf.value[0][0], 'crypttab')

        # check for present fstab configuration item on a cleartext block device
        conf = self.get_property(luks_block, '.Block', 'Configuration')
        conf.assertTrue()
        self.assertEqual(conf.value[0][0], 'fstab')

        child_conf = self.get_property(self.lv_block, '.Encrypted', 'ChildConfiguration')
        child_conf.assertTrue()
        self.assertEqual(child_conf.value[0][0], 'fstab')
        self.assertEqual(child_conf.value, conf.value)

        # check that fstab and crypttab records have been added
        crypttab = self.read_file('/etc/crypttab')
        self.assertIn(vgname, crypttab)
        self.assertIn(self.luks_uuid, crypttab)
        fstab = self.read_file('/etc/fstab')
        self.assertIn(vgname, fstab)
        self.assertIn(self.fs_uuid, fstab)

    def _check_torn_down_stack(self, name):
        # check that all created objects don't exist anymore
        msg = r'Object does not exist at path|No such interface'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            luks_block = self.get_object(self.luks_block_path)
            self.get_property_raw(luks_block, '.Block', 'DeviceNumber')
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            lv_block = self.get_object(self.lv_block_path)
            self.get_property_raw(lv_block, '.Block', 'DeviceNumber')
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            # the lvm2 udisks module is not fully synchronous, see https://github.com/storaged-project/udisks/pull/814
            time.sleep(2)
            lv = self.get_object(self.lv_path)
            self.get_property_raw(lv, '.LogicalVolume', 'Name')
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            vg = self.get_object(self.vg_path)
            self.get_property_raw(vg, '.VolumeGroup', 'Name')

        # check that fstab and crypttab records have been removed
        crypttab = self.read_file('/etc/crypttab')
        self.assertNotIn(name, crypttab)
        self.assertNotIn(self.luks_uuid, crypttab)
        fstab = self.read_file('/etc/fstab')
        self.assertNotIn(name, fstab)
        self.assertNotIn(self.fs_uuid, fstab)


    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_teardown_active_vg_unlocked(self):
        ''' Test tear-down by removing the base VG (not deactivated, unlocked) '''

        name = 'udisks_test_teardown_active_vg_unlocked'

        self._init_stack(name)

        self._remove_vg(self.vg, tear_down=True, ignore_removed=False)

        self._check_torn_down_stack(name)

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_teardown_active_vg_locked(self):
        ''' Test tear-down by removing the base VG (not deactivated, locked) '''

        name = 'udisks_test_teardown_active_vg_locked'

        self._init_stack(name)

        self.lv_block.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
        self._remove_vg(self.vg, tear_down=True, ignore_removed=False)

        self._check_torn_down_stack(name)

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_teardown_inactive_vg_locked(self):
        ''' Test tear-down by removing the base VG (deactivated, locked) '''

        name = 'udisks_test_teardown_inactive_locked'

        self._init_stack(name)

        self.lv_block.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
        self.lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')
        self._remove_vg(self.vg, tear_down=True, ignore_removed=False)

        self._check_torn_down_stack(name)

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_reformat_inactive_vg_locked(self):
        ''' Test tear-down by re-formatting the base PV (VG deactivated, locked) '''

        name = 'test_reformat_inactive_vg_locked'

        self._init_stack(name)

        self.lv_block.Lock(self.no_options, dbus_interface=self.iface_prefix + '.Encrypted')
        self.lv.Deactivate(self.no_options, dbus_interface=self.iface_prefix + '.LogicalVolume')

        # now reformat the PV with tear-down flag
        options = dbus.Dictionary(signature='sv')
        options['label'] = 'AFTER_TEARDOWN'
        options['tear-down'] = dbus.Boolean(True)

        self.pv.Format('ext4', options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # TODO: implement proper teardown across combined LVM + LUKS stack
        # https://github.com/storaged-project/udisks/issues/781

        # check that fstab and crypttab records have been removed
        # TODO: these checks are the opposite - record shouldn't be present, once this is fixed
        # self._check_torn_down_stack(name)
        crypttab = self.read_file('/etc/crypttab')
        self.assertIn(name, crypttab)
        self.assertIn(self.luks_uuid, crypttab)
        fstab = self.read_file('/etc/fstab')
        self.assertIn(name, fstab)
        self.assertIn(self.fs_uuid, fstab)
