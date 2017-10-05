import os
import time
import threading

import gi
gi.require_version('GLib', '2.0')
from gi.repository import GLib

import safe_dbus
import udiskstestcase


class UdisksJobTest(udiskstestcase.UdisksTestCase):
    '''This is a basic test suite for job objects and interface'''

    def setUp(self):
        self.job = None
        self.exception = None

    def _get_objects(self):
        objects = safe_dbus.call_sync(self.iface_prefix,
                                      self.path_prefix,
                                      'org.freedesktop.DBus.ObjectManager',
                                      'GetManagedObjects',
                                      None)
        return objects

    def _secure_erase(self, devname):
        try:
            safe_dbus.call_sync(self.iface_prefix,
                                self.path_prefix + '/block_devices/' + devname,
                                self.iface_prefix + '.Block',
                                'Format',
                                GLib.Variant('(sa{sv})', ('empty', {'erase': GLib.Variant("s", 'zero')})))
        except Exception as e:
            self.exception = e

    def _wait_for_job_thread(self, operation, device_path):
        t = threading.currentThread()

        while getattr(t, "run", True):
            objects = self._get_objects()

            jobs = {k: v for (k, v) in objects[0].items() if '/jobs/' in k}

            for job_path, properties in jobs.items():
                if properties[self.iface_prefix + '.Job']['Operation'] == operation and \
                   properties[self.iface_prefix + '.Job']['Objects'] == [device_path]:
                    self.job = (job_path, properties)
                    return

            time.sleep(0.1)

    def test_job(self):
        '''Test basic Job functionality and properties'''

        disk_name = os.path.basename(self.vdevs[0])
        obj_path = self.path_prefix + '/block_devices/' + disk_name

        start_time = time.time()

        watch_thread = threading.Thread(target=self._wait_for_job_thread, args=('format-erase', obj_path))
        watch_thread.start()

        erase_thread = threading.Thread(target=self._secure_erase, args=(disk_name,))
        erase_thread.start()
        erase_thread.join()

        # erase thread finished, stop job searching
        watch_thread.run = False
        watch_thread.join()

        # unexpected exception occured in erase thread -- raise it
        # timeout reached is actually ok -- zeroing the device may take a long
        # time, but we just want to check the job object
        if self.exception is not None and not str(self.exception).endswith('Timeout was reached'):
            raise self.exception

        # we should have the job dict now
        self.assertIsNotNone(self.job)

        # get all the properties from the dict
        properties = self.job[1][self.iface_prefix + '.Job']

        _ret, disk_size = self.run_command('lsblk -d -b -no SIZE %s' % self.vdevs[0])  # get size of the device
        self.assertEqual(properties['Bytes'], int(disk_size))

        self.assertEqual(properties['StartedByUID'], os.getuid())

        # test start time -- should be less than ~0.5s after thread started
        # (dbus property is in micro seconds)
        self.assertLessEqual(abs(properties['StartTime'] - start_time * 10**6), 0.5 * 10**6)

        self.assertTrue(properties['Cancelable'])

        # job still exists -- erase call timed out -- try to cancel it
        if safe_dbus.check_object_available(self.iface_prefix, self.job[0],
                                            self.iface_prefix + '.Job'):
            try:
                safe_dbus.call_sync(self.iface_prefix,
                                    self.job[0],
                                    self.iface_prefix + '.Job',
                                    'Cancel',
                                    GLib.Variant('(a{sv})', ({},)))
            except safe_dbus.DBusCallError:
                pass

    def test_cancel(self):
        '''Test if it's possible to cancel the Job'''

        disk_name = os.path.basename(self.vdevs[0])
        obj_path = self.path_prefix + '/block_devices/' + disk_name

        watch_thread = threading.Thread(target=self._wait_for_job_thread, args=('format-erase', obj_path))
        watch_thread.start()

        erase_thread = threading.Thread(target=self._secure_erase, args=(disk_name,))
        erase_thread.start()

        # watch thread should end first -- we need the job before erase finishes
        watch_thread.join(timeout=10)
        if not self.job:
            watch_thread.run = False  # stop the watch thread now
            if self.exception:  # exception in erase thread -- just raise it
                raise self.exception
            else:  # didn't find the job but no exception
                self.fail('Failed to find the job objects.')

        job_path = self.job[0]

        # cancel the job
        safe_dbus.call_sync(self.iface_prefix,
                            job_path,
                            self.iface_prefix + '.Job',
                            'Cancel',
                            GLib.Variant('(a{sv})', ({},)))

        # job shouldn't be in managed objects
        objects = self._get_objects()
        self.assertNotIn(job_path, objects[0].items())

        erase_thread.join()

        # erase thread should raise exception (it's stored in self.exception)
        self.assertIsNotNone(self.exception)
        self.assertTrue(isinstance(self.exception, safe_dbus.DBusCallError))
        self.assertIn('Error erasing device: Job was canceled', str(self.exception))
