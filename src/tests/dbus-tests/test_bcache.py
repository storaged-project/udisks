import os
import re
import time
import unittest

import udiskstestcase


BLOCK_SIZE = 512


class UdisksBcacheTest(udiskstestcase.UdisksTestCase):
    '''This is a basic bcache test suite'''

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.check_module_loaded('Bcache'):
            udiskstestcase.UdisksTestCase.tearDownClass()
            raise unittest.SkipTest('Udisks module for bcache tests not loaded, skipping.')

    def setUp(self):
        self.skipTest("bcache setup fails randomly due to some issue in kernel or udev")
        self.bcaches = self._get_bcaches()
        super().setUp()

    def _get_bcaches(self):
        return {name for name in os.listdir('/dev') if re.match(r'^bcache[0-9]+$', name)}

    def _handle_create_fail(self, backing_dev, cache_dev):
        # BcacheCreate sometimes fail after the bcache device is created, we
        # need to delete it, because it breaks other tests
        time.sleep(5)
        bcache = self._get_bcaches() - self.bcaches
        if len(bcache) == 1:
            self._force_remove(bcache.pop(), backing_dev, cache_dev)

    def _force_remove(self, bcache_name, backing_dev, cache_dev):
        _ret, out = self.run_command('bcache-super-show %s | grep cset.uuid ' % cache_dev)
        cset_uuid = out.split()[-1]

        self.write_file('/sys/block/%s/bcache/detach' % bcache_name, cset_uuid, True)
        self.write_file('/sys/fs/bcache/%s/stop' % cset_uuid, '1', True)
        self.write_file('/sys/block/%s/bcache/stop' % bcache_name, '1', True)

        if not self.wipe_fs(cache_dev):
            self.fail('Failed to wipe cache device %s after removing bcache.' % cache_dev)
        if not self.wipe_fs(backing_dev):
            self.fail('Failed to wipe backing device %s after removing bcache.' % backing_dev)

    def _get_mode(self, bcache_name):
        # cache_mode contains all cache modes, the right one is in square brackets
        modes = self.read_file('/sys/block/%s/bcache/cache_mode' % bcache_name)
        return next((x[1:-1] for x in modes.split() if re.match(r'^\[.*\]$', x)), None)

    def _get_size(self, bcache_name):
        cache_dir = '/sys/block/%s/bcache/cache' % bcache_name

        # sum sizes from all caches
        caches = ['%s/%s' % (cache_dir, d) for d in os.listdir(cache_dir) if re.match('cache[0-9]*$', d)]
        size = sum(int(self.read_file(os.path.realpath(c) + '/../size')) for c in caches)

        return size

    def _obj_path_from_path(self, device_path):
        return self.path_prefix + '/block_devices/' + os.path.basename(device_path)

    def test_create_destroy(self):
        '''Test creating a new bcache and its properties'''

        manager = self.get_object('/Manager')
        try:
            bcache_path = manager.BcacheCreate(self._obj_path_from_path(self.vdevs[0]),
                                               self._obj_path_from_path(self.vdevs[1]), self.no_options,
                                               dbus_interface=self.iface_prefix + '.Manager.Bcache')
        except Exception as e:
            self._handle_create_fail(self.vdevs[0], self.vdevs[1])
            raise e

        self.assertIsNotNone(bcache_path)
        bcache_name = bcache_path.split('/')[-1]
        self.addCleanup(self._force_remove, bcache_name, self.vdevs[0], self.vdevs[1])

        bcache = self.get_object('/block_devices/' + bcache_name)

        self.assertTrue(os.path.exists('/sys/block/%s/bcache' % bcache_name))

        # check properties
        sys_mode = self._get_mode(bcache_name)
        dbus_mode = self.get_property(bcache, '.Block.Bcache', 'Mode')
        dbus_mode.assertEqual(sys_mode, timeout=15)

        sys_state = self.read_file('/sys/block/%s/bcache/state' % bcache_name).strip()
        dbus_state = self.get_property(bcache, '.Block.Bcache', 'State')
        dbus_state.assertEqual(sys_state)

        sys_block = self.read_file('/sys/block/%s/bcache/cache/block_size' % bcache_name)
        dbus_block = self.get_property(bcache, '.Block.Bcache', 'BlockSize')
        dbus_block.assertEqual(int(sys_block))

        sys_size = self._get_size(bcache_name)
        dbus_size = self.get_property(bcache, '.Block.Bcache', 'CacheSize')
        dbus_size.assertEqual(sys_size * BLOCK_SIZE)

        sys_hits = self.read_file('/sys/block/%s/bcache/cache/stats_total' \
                                  '/cache_hits' % bcache_name)
        dbus_hits = self.get_property(bcache, '.Block.Bcache', 'Hits')
        dbus_hits.assertEqual(int(sys_hits))

        sys_misses = self.read_file('/sys/block/%s/bcache/cache/stats_total' \
                                    '/cache_misses' % bcache_name)
        dbus_misses = self.get_property(bcache, '.Block.Bcache', 'Misses')
        dbus_misses.assertEqual(int(sys_misses))

        sys_byhits = self.read_file('/sys/block/%s/bcache/cache/stats_total' \
                                    '/cache_bypass_hits' % bcache_name)
        dbus_byhits = self.get_property(bcache, '.Block.Bcache', 'BypassHits')
        dbus_byhits.assertEqual(int(sys_byhits))

        sys_bymisses = self.read_file('/sys/block/%s/bcache/cache/stats_total' \
                                      '/cache_bypass_misses' % bcache_name)
        dbus_bymisses = self.get_property(bcache, '.Block.Bcache', 'BypassMisses')
        dbus_bymisses.assertEqual(int(sys_bymisses))

        # destroy the cache
        bcache.BcacheDestroy(self.no_options, dbus_interface=self.iface_prefix + '.Block.Bcache')

        # make sure the bcache device is not on dbus
        udisks = self.get_object('')
        objects = udisks.GetManagedObjects(dbus_interface='org.freedesktop.DBus.ObjectManager')
        self.assertNotIn(str(bcache.object_path), objects.keys())

        self.assertFalse(os.path.exists('/dev/%s' % bcache_name))

    def test_set_mode(self):
        '''Test if it's possible to change cache mode on existing bcache'''

        manager = self.get_object('/Manager')
        try:
            bcache_path = manager.BcacheCreate(self._obj_path_from_path(self.vdevs[0]),
                                               self._obj_path_from_path(self.vdevs[1]), self.no_options,
                                               dbus_interface=self.iface_prefix + '.Manager.Bcache')
        except Exception as e:
            self._handle_create_fail(self.vdevs[0], self.vdevs[1])
            raise e

        self.assertIsNotNone(bcache_path)
        bcache_name = bcache_path.split('/')[-1]
        self.addCleanup(self._force_remove, bcache_name, self.vdevs[0], self.vdevs[1])

        bcache = self.get_object('/block_devices/' + bcache_name)
        bcache.SetMode('writeback', self.no_options,
                       dbus_interface=self.iface_prefix + '.Block.Bcache')

        dbus_mode = self.get_property(bcache, '.Block.Bcache', 'Mode')
        dbus_mode.assertEqual('writeback')

        sys_mode = self._get_mode(bcache_name)
        self.assertEqual(sys_mode, 'writeback')
