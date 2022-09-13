import dbus
import os
import errno
import re
import six
import shutil
import tempfile
import unittest
import time
from packaging.version import Version

from multiprocessing import Process, Pipe

import gi
gi.require_version('GLib', '2.0')
gi.require_version('BlockDev', '2.0')
from gi.repository import GLib
from gi.repository import BlockDev

import safe_dbus
import udiskstestcase


class UdisksFSTestCase(udiskstestcase.UdisksTestCase):
    _fs_signature = None
    _fs_name = None
    _can_create = False
    _can_label = False  # it is possible to set label when *creating* filesystem
    _can_relabel = False  # it is possible to set label for *existing* filesystem
    _can_mount = False
    _can_query_size = False

    username = 'udisks_test_user'

    def _rmtree(self, path):
        for _ in range(10):
            try:
                shutil.rmtree(path)
                break
            except OSError as e:
                if e.errno == errno.EBUSY:
                    time.sleep(0.5)
                    continue
                raise

    def _add_user(self, username):
        ret, out = self.run_command('useradd -M -p "" %s' % username)
        if ret != 0:
            self.fail('Failed to create user %s: %s' % (username, out))

        ret, uid = self.run_command('id -u %s' % username)
        if ret != 0:
            self.fail('Failed to get UID for user %s' % username)

        ret, gid = self.run_command('id -g %s' % username)
        if ret != 0:
            self.fail('Failed to get GID for user %s.' % username)

        return (uid, gid)

    def _remove_user(self, username):
        ret, out = self.run_command('userdel %s' % username)
        if ret != 0:
            self.fail('Failed to remove user user %s: %s' % (username, out))

    def _get_libmount_version(self):
        _ret, out = self.run_command('mount --version')
        m = re.search(r'libmount ([\d\.]+)', out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError('Failed to determine libmount version from: %s' % out)
        return Version(m.groups()[0])

    def _get_mount_options_conf_path(self):
        if os.environ["UDISKS_TESTS_ARG_SYSTEM"] == "1":
            return "/etc/udisks2/mount_options.conf"
        else:
            return os.path.join(os.environ["UDISKS_TESTS_PROJDIR"], 'udisks/mount_options.conf')

    @classmethod
    def command_exists(cls, command):
        ret, _out = cls.run_command('type %s' % command)
        return ret == 0

    def _creates_protective_part_table(self):
        """ Indicates whether mkfs creates a protective partition table. """
        return False

    def _get_formatted_block_object(self, dev_path):
        """ Get the real block object and its device path for a given filesystem type after formatting. """
        if self._creates_protective_part_table() and not dev_path.endswith("1") and \
           not self._fs_signature == "vfat":  # udisksd forces --mbr=n for vfat
            dev_path += "1"
        block_object = self.get_object('/block_devices/' + os.path.basename(dev_path))
        return (block_object, dev_path)

    def _create_format(self, block_object, options=None):
        if not options:
            options = self.no_options

        # create filesystem
        block_object.Format(self._fs_signature, options, dbus_interface=self.iface_prefix + '.Block')

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # test dbus properties
        usage = self.get_property(block_fs, '.Block', 'IdUsage')
        usage.assertEqual('filesystem')

        fstype = self.get_property(block_fs, '.Block', 'IdType')
        fstype.assertEqual(self._fs_signature)

        # test system values
        _ret, sys_fstype = self.run_command('lsblk -d -no FSTYPE %s' % block_fs_dev)
        self.assertEqual(sys_fstype, self._fs_signature)

    def test_create_format(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # create filesystem for the first time
        self._create_format(disk)
        # and now create it again, let the daemon handle wiping
        self._create_format(disk)

    def test_create_format_nodiscard(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # unfortunately no way to check if the option was really passed to the
        # mkfs command, so just a "sanity" test here
        options = dbus.Dictionary(signature='sv')
        options['no-discard'] = True
        self._create_format(disk, options=options)

    def _invalid_label(self, disk):
        pass

    def test_label(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_label:
            self.skipTest('Cannot set label when creating %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem with label
        label = 'TEST' if self._fs_signature == 'vfat' else 'test'  # XXX mkfs.vfat changes labels to uppercase
        d = dbus.Dictionary(signature='sv')
        d['label'] = label
        disk.Format(self._fs_signature, d, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # test dbus properties
        dbus_label = self.get_property(block_fs, '.Block', 'IdLabel')
        dbus_label.assertEqual(label)

        # test system values
        _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % block_fs_dev)
        self.assertEqual(sys_label, label)

    def test_relabel(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_relabel:
            self.skipTest('Cannot set label on an existing %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem with label
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # change the label
        label = 'AAAA' if self._fs_signature == 'vfat' else 'aaaa'  # XXX udisks changes vfat labels to uppercase
        block_fs.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

        # test dbus properties
        dbus_label = self.get_property(block_fs, '.Block', 'IdLabel')
        dbus_label.assertEqual(label)

        # test system values
        _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % block_fs_dev)
        self.assertEqual(sys_label, label)

        # test invalid label behaviour
        self._invalid_label(block_fs)

    def test_repair_resize_check(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        if not self._can_query_size:
            self.skipTest('Cannot determine size of %s filesystem' % self._fs_signature)

        manager = self.get_interface(self.get_object('/Manager'), '.Manager')
        try:
          rep, mode, _ = manager.CanResize(self._fs_signature)
          chk, _ = manager.CanCheck(self._fs_signature)
          rpr, _ = manager.CanRepair(self._fs_signature)
        except:
          rpr = chk = rep = False
        if not (rpr and chk and rep) or mode & (1 << 1) == 0:
            self.skipTest('Cannot check, offline-shrink and repair %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # not mounted
        mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        mounts.assertLen(0)

        # repair before resizing to half size, then check
        self.assertTrue(block_fs.Repair(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem'))
        size = self.get_property(block_fs, '.Block', 'Size').value
        self.get_property(block_fs, '.Filesystem', 'Size').assertAlmostEqual(size, delta=1024**2)
        block_fs.Resize(dbus.UInt64(size // 2), self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        if self._fs_signature == "ntfs":
            # workaround for ntfsprogs, requiring one more repair pass after resize
            self.assertTrue(block_fs.Repair(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem'))
        self.assertTrue(block_fs.Check(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem'))
        self.get_property(block_fs, '.Filesystem', 'Size').assertAlmostEqual(size // 2, delta=1024**2)

    def test_size(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        if not self._can_query_size:
            self.skipTest('Cannot determine size of %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # mount, except for ntfs which can't tell us size when mounted...
        if self._fs_signature not in ["ntfs", "exfat"]:
            d = dbus.Dictionary(signature='sv')
            if self._fs_name:
                d['fstype'] = self._fs_name
            d['options'] = 'ro'
            mnt_path = block_fs.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
            self.addCleanup(self.try_unmount, block_fs_dev)
            self.addCleanup(self.try_unmount, self.vdevs[0])

        # check reported size
        size = self.get_property(block_fs, '.Block', 'Size').value
        self.get_property(block_fs, '.Filesystem', 'Size').assertAlmostEqual(size, delta=1024**2)

    def test_mount_auto(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # not mounted
        mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        mounts.assertLen(0)

        # mount
        d = dbus.Dictionary(signature='sv')
        d['options'] = 'ro'
        if self._fs_name:
            d['fstype'] = self._fs_name
        mnt_path = block_fs.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, block_fs_dev)
        self.addCleanup(self.try_unmount, self.vdevs[0])

        # system mountpoint
        self.assertTrue(os.path.ismount(mnt_path))
        _ret, out = self.run_command('mount | grep %s' % block_fs_dev)
        self.assertIn(mnt_path, out)
        self.assertIn('ro', out)
        if self._fs_signature.startswith('ext'):
            self.assertIn('errors=remount-ro', out)

        # dbus mountpoint
        dbus_mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        dbus_mnt = self.ay_to_str(dbus_mounts.value[0])  # mountpoints are arrays of bytes
        self.assertEqual(dbus_mnt, mnt_path)

        # umount
        block_fs.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.assertFalse(os.path.ismount(mnt_path))

        # try mounting with 'fstype' = 'auto'
        d['fstype'] = 'auto'
        mnt_path = block_fs.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
        dbus_mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        dbus_mnt = self.ay_to_str(dbus_mounts.value[0])  # mountpoints are arrays of bytes
        self.assertEqual(dbus_mnt, mnt_path)
        block_fs.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_auto_configurable_mount_options(self):
        def test_readonly(self, should_be_readonly, config_file_contents, udev_rules_content=None, ignore_fstype=False):
            try:
                self.write_file(conf_file_path, config_file_contents)
                if udev_rules_content is not None:
                    self.set_udev_properties(block_fs_dev, udev_rules_content)
                dd = dbus.Dictionary(signature='sv')
                if self._fs_name and not ignore_fstype:
                    dd['fstype'] = self._fs_name
                mnt_path = block_fs.Mount(dd, dbus_interface=self.iface_prefix + '.Filesystem')
                self.assertTrue(os.path.ismount(mnt_path))
                if should_be_readonly:
                    with six.assertRaisesRegex(self, OSError, "Read-only file system"):
                        fd, _tmpfile = tempfile.mkstemp(dir=mnt_path)
                        os.close(fd)
                else:
                    fd, _tmpfile = tempfile.mkstemp(dir=mnt_path)
                    os.close(fd)
                block_fs.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
                self.assertFalse(os.path.ismount(mnt_path))
            finally:
                if udev_rules_content is not None:
                    self.set_udev_properties(block_fs_dev, None)

        def test_custom_option(self, should_fail, dbus_option, should_be_present, config_file_contents, udev_rules_content=None, match_mount_option=None, ignore_fstype=False):
            try:
                self.write_file(conf_file_path, config_file_contents)
                if udev_rules_content is not None:
                    self.set_udev_properties(block_fs_dev, udev_rules_content)
                dd = dbus.Dictionary(signature='sv')
                if self._fs_name and not ignore_fstype:
                    dd['fstype'] = self._fs_name
                if dbus_option is not None:
                    dd['options'] = dbus_option
                if should_fail:
                    if dbus_option is not None:
                        msg="org.freedesktop.UDisks2.Error.OptionNotPermitted: Mount option `%s' is not allowed" % dbus_option
                    else:
                        msg="org.freedesktop.UDisks2.Error.OptionNotPermitted: Mount option `.*' is not allowed"
                    with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
                        mnt_path = block_fs.Mount(dd, dbus_interface=self.iface_prefix + '.Filesystem')
                else:
                    mnt_path = block_fs.Mount(dd, dbus_interface=self.iface_prefix + '.Filesystem')
                    self.assertTrue(os.path.ismount(mnt_path))
                    _ret, out = self.run_command('mount | grep %s | sed "s/.*(\(.*\))/\\1/"' % block_fs_dev)
                    if dbus_option is not None:
                        if should_be_present:
                            self.assertIn(dbus_option, out)
                        else:
                            self.assertNotIn(dbus_option, out)
                    if match_mount_option is not None:
                        if should_be_present:
                            self.assertIn(match_mount_option, out)
                        else:
                            self.assertNotIn(match_mount_option, out)
                    block_fs.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
                    self.assertFalse(os.path.ismount(mnt_path))
            finally:
                if udev_rules_content is not None:
                    self.set_udev_properties(block_fs_dev, None)


        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        # changing `mount_options.conf`, make a backup and restore on cleanup
        try:
            conf_file_path = self._get_mount_options_conf_path()
            conf_file_backup = self.read_file(conf_file_path)
            self.addCleanup(self.write_file, conf_file_path, conf_file_backup)
        except FileNotFoundError as e:
            # no existing mount_options.conf, simply remove the file once finished
            self.addCleanup(self.remove_file, conf_file_path, True)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # not mounted
        mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        mounts.assertLen(0)
        self.addCleanup(self.try_unmount, self.vdevs[0])
        self.addCleanup(self.try_unmount, block_fs_dev)

        if self._fs_name and self._fs_name != self._fs_signature:
            _fs_id = "%s:%s" % (self._fs_signature, self._fs_name)
        else:
            _fs_id = self._fs_signature

        # read-write mount
        test_readonly(self, False, "")
        # read-only mount by global defaults
        test_readonly(self, True, "[defaults]\ndefaults=ro\n")
        # read-only mount by global filesystem type specific options
        test_readonly(self, True, "[defaults]\ndefaults=\n%s_defaults=ro\n" % _fs_id)
        # false positive of the previous one
        test_readonly(self, False, "[defaults]\n%sx_defaults=ro\n" % _fs_id)
        # block device defaults and overrides
        test_readonly(self, False, "[defaults]\ndefaults=ro\n\n[%s]\ndefaults=\n" % block_fs_dev)
        test_readonly(self, False, "[defaults]\ndefaults=ro\n\n[%s]\ndefaults=rw\n" % block_fs_dev)
        test_readonly(self, True, "[defaults]\ndefaults=ro\n\n[%sx]\ndefaults=\n" % block_fs_dev)
        test_readonly(self, True, "[defaults]\ndefaults=ro\n\n[%sx]\ndefaults=rw\n" % block_fs_dev)
        test_readonly(self, True, "[%s]\ndefaults=ro\n" % block_fs_dev)
        if self._fs_name == "ntfs3":
            test_readonly(self, False, "[defaults]\nntfs_defaults=ro\n\n[%sx]\nntfs:ntfs3_defaults=rw\n" % block_fs_dev)
            test_readonly(self, False, "[defaults]\nntfs:ntfs_defaults=ro\n\n[%sx]\nntfs:ntfs3_defaults=rw\n" % block_fs_dev)
            test_readonly(self, True,  "[defaults]\ndefaults=ro\nntfs_defaults=ro\n\n[%sx]\nntfs:ntfs3_defaults=rw\n" % block_fs_dev)
            test_readonly(self, True,  "[defaults]\ndefaults=ro\nntfs:ntfs_defaults=ro\n\n[%sx]\nntfs:ntfs3_defaults=rw\n" % block_fs_dev)
            if self._have_ntfs3g:
                test_readonly(self, False, "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=rw\nntfs_drivers=ntfs3,ntfs\n", ignore_fstype=True)
                test_readonly(self, False, "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=rw\nntfs_drivers=ntfs3\n", ignore_fstype=True)
                test_readonly(self, True,  "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=rw\nntfs_drivers=ntfs,ntfs3\n", ignore_fstype=True)
                test_readonly(self, True,  "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=rw\nntfs_drivers=ntfs\n", ignore_fstype=True)
                test_readonly(self, False, "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=rw\n", ignore_fstype=True)
                test_readonly(self, False, "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=ro\nntfs_drivers=ntfs3,ntfs\n[%s]\nntfs:ntfs3_defaults=rw\n" % block_fs_dev, ignore_fstype=True)
                test_readonly(self, True,  "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=ro\nntfs_drivers=ntfs,ntfs3\n[%s]\nntfs:ntfs3_defaults=rw\n" % block_fs_dev, ignore_fstype=True)
                test_readonly(self, False, "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=ro\n[%s]\nntfs:ntfs3_defaults=rw\nntfs_drivers=ntfs3,ntfs\n" % block_fs_dev, ignore_fstype=True)
                test_readonly(self, True,  "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=ro\n[%s]\nntfs:ntfs3_defaults=rw\nntfs_drivers=ntfs,ntfs3\n" % block_fs_dev, ignore_fstype=True)
                test_readonly(self, False, "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=rw\n[%s]\nntfs_drivers=ntfs3,ntfs\n" % block_fs_dev, ignore_fstype=True)
                test_readonly(self, True,  "[defaults]\nntfs_defaults=ro\nntfs:ntfs3_defaults=rw\n[%s]\nntfs_drivers=ntfs,ntfs3\n" % block_fs_dev, ignore_fstype=True)
        # block device fs-specific options
        test_readonly(self, True, "[defaults]\ndefaults=ro\n%s_defaults=ro\n\n[%s]\ndefaults=\n" % (_fs_id, block_fs_dev))
        test_readonly(self, True, "[defaults]\ndefaults=ro\n{0}_defaults=ro\n\n[{1}]\n{0}_defaults=\n".format(_fs_id, block_fs_dev))
        test_readonly(self, False, "[defaults]\ndefaults=ro\n{0}_defaults=ro\n\n[{1}]\ndefaults=\n{0}_defaults=\n".format(_fs_id, block_fs_dev))

        # standalone custom option presence
        test_custom_option(self, False, None, False, "")
        test_custom_option(self, True, "nonsense", False, "")
        # config file custom option presence
        test_custom_option(self, True, None, False, "[defaults]\ndefaults=nonsense\n")
        # disallow rw
        test_custom_option(self, True, None, False, "[defaults]\ndefaults=rw\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,sync,dirsync,noload\n")
        test_custom_option(self, True, None, False, "[defaults]\ndefaults=ro\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,rw,sync,dirsync,noload\n")
        test_custom_option(self, False, "rw", True, "[defaults]\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,rw,sync,dirsync,noload\n")
        test_custom_option(self, True,  "rw", True, "[defaults]\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,sync,dirsync,noload\n")
        test_custom_option(self, False, "rw", True, "[defaults]\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,sync,dirsync,noload\n\n[%s]\nallow=ro,rw\n" % block_fs_dev)
        test_custom_option(self, True,  "rw", True, "[defaults]\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,sync,dirsync,noload\n\n[%s]\nallow=ro\n" % block_fs_dev)
        test_custom_option(self, True,  "rw", True, "[defaults]\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,sync,dirsync,noload\n\n[%s]\nallow=dfkjhdsjkfhsdjkfhsdahf\n" % block_fs_dev)
        test_custom_option(self, True,  "rw", True, "[defaults]\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,sync,dirsync,noload\n\n[{1}]\n{0}_allow=\n{0}_defaults=\n".format(_fs_id, block_fs_dev))
        test_custom_option(self, True,  "rw", True, "[defaults]\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,sync,dirsync,noload\n\n[{1}]\n{0}_allow=\n{0}_defaults=rw\n".format(_fs_id, block_fs_dev))
        test_custom_option(self, False, "rw", True, "[defaults]\n\n[{1}]\n{0}_allow=rw\n{0}_defaults=rw\n".format(_fs_id, block_fs_dev))
        # uid and gid passing
        if self._fs_signature in ["vfat", "exfat", "ntfs"]:
            test_custom_option(self, False, None, False, "[defaults]\ndefaults=uid=\n")
            test_custom_option(self, False, None, False, "[defaults]\ndefaults=uid=,gid=\n")
            test_custom_option(self, True,  None, False, "[defaults]\ndefaults=xuid=\n")
            test_custom_option(self, True,  None, False, "[defaults]\ndefaults=uid=,xuid=,gid=\n")
            test_custom_option(self, False, None, False, "[defaults]\ndefaults=uid=596\n")
        if self._fs_signature in ["vfat", "exfat", "udf"]:
            test_custom_option(self, True, "uid=10", True, "[defaults]\nallow=uid=$UID,gid=$GID,exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,rw,sync,dirsync,noload,uid=ignore\n")
            test_custom_option(self, False, "uid=10", True, "[defaults]\nallow=uid=$UID,gid=$GID,exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,rw,sync,dirsync,noload,uid=ignore,uid=10\n")
        # fs-specific mount options
        if self._fs_signature == "vfat":
            test_custom_option(self, False, None, True, "", match_mount_option="flush")
            test_custom_option(self, False, None, False, "[defaults]\nvfat_defaults=uid=,gid=,shortname=mixed,utf8=1,showexec\n", match_mount_option="flush")
        if self._fs_signature == "udf":
            test_custom_option(self, False, None, False, "[defaults]\ndefaults=\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,rw,sync,dirsync,noload,uid=ignore,uid=forget\n")
            test_custom_option(self, True, "uid=notallowed", True, "[defaults]\nallow=exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,rw,sync,dirsync,noload,uid=ignore\n")
        if self._fs_signature.startswith("ext"):
            test_custom_option(self, False, "errors=remount-ro", True, "", match_mount_option="errors=remount-ro")
            test_custom_option(self, True, "errors=panic", False, "")
            test_custom_option(self, True, "errors=continue", False, "")

        # udev rules overrides
        test_readonly(self, False, "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_DEFAULTS": "rw" })
        test_readonly(self, True,  "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_DEFAULTS": "ro" })
        test_readonly(self, True,  "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_%s_DEFAULTS" % _fs_id.upper(): "ro" })
        test_readonly(self, False, "[defaults]\ndefaults=ro\n", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_DEFAULTS": "rw" })
        test_readonly(self, True,  "[defaults]\n%s_defaults=nonsense\n" % _fs_id, udev_rules_content = { "UDISKS_MOUNT_OPTIONS_%s_DEFAULTS" % _fs_id.upper(): "ro" })
        test_readonly(self, False, "[defaults]\ndefaults=ro\n\n[%s]\ndefaults=nonsense\n" % block_fs_dev, udev_rules_content = { "UDISKS_MOUNT_OPTIONS_DEFAULTS": "rw" })
        test_custom_option(self, True,  None, False, "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_DEFAULTS": "nonsense" })
        # disallow rw
        test_custom_option(self, True, None, False, "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_DEFAULTS": "rw", "UDISKS_MOUNT_OPTIONS_ALLOW": "exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,sync,dirsync,noload" })
        test_custom_option(self, True, None, False, "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_DEFAULTS": "ro", "UDISKS_MOUNT_OPTIONS_ALLOW": "exec,noexec,nodev,nosuid,atime,noatime,nodiratime,rw,sync,dirsync,noload" })
        test_custom_option(self, False, "rw", True, "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_ALLOW": "exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,rw,sync,dirsync,noload" })
        test_custom_option(self, True,  "rw", True, "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_ALLOW": "exec,noexec,nodev,nosuid,atime,noatime,nodiratime,ro,sync,dirsync,noload" })
        # fs-specific mount options
        if self._fs_signature == "vfat":
            test_custom_option(self, False, None, True, "", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_VFAT_DEFAULTS": "uid=,gid=,shortname=mixed,utf8=1,showexec,flush" }, match_mount_option="flush")
            test_custom_option(self, False, None, False, "[defaults]\nvfat_defaults=uid=,gid=,shortname=mixed,utf8=1,showexec,flush\n", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_VFAT_DEFAULTS": "uid=,gid=,shortname=mixed,utf8=1,showexec" }, match_mount_option="flush")
            test_custom_option(self, False, None, True,  "[defaults]\nvfat_defaults=uid=,gid=,shortname=mixed,utf8=1,showexec\n", udev_rules_content = { "UDISKS_MOUNT_OPTIONS_VFAT_DEFAULTS": "uid=,gid=,shortname=mixed,utf8=1,showexec,flush" }, match_mount_option="flush")
            test_custom_option(self, False, None, True,  "[defaults]\nvfat_defaults=xxxxx\n\n[%s]\nvfat_defaults=yyyyyy\n" % block_fs_dev, udev_rules_content = { "UDISKS_MOUNT_OPTIONS_VFAT_DEFAULTS": "uid=,gid=,shortname=mixed,utf8=1,showexec,flush" }, match_mount_option="flush")


    def _test_fstab_label(self, disk_obj_path, label, fstab_label_str, mount_should_fail):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if label:
            if not self._can_label:
                self.skipTest('Cannot set label when creating %s filesystem' % self._fs_signature)
            if self._fs_signature == 'vfat':
                self.skipTest('VFAT has strict label rules, skipping')

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        # this test will change /etc/fstab, we might want to revert the changes after it finishes
        fstab = self.read_file('/etc/fstab')
        self.addCleanup(self.write_file, '/etc/fstab', fstab)

        disk = self.get_object(disk_obj_path)
        self.assertIsNotNone(disk)
        disk_dev_path = self.ay_to_str(self.get_property_raw(disk, '.Block', 'Device'))

        d = self.no_options
        if label:
            d = dbus.Dictionary(signature='sv')
            d['label'] = label

        # create filesystem
        disk.Format(self._fs_signature, d, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, disk_dev_path)

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(disk_dev_path)
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        if label:
            # test dbus properties
            dbus_label = self.get_property(block_fs, '.Block', 'IdLabel')
            dbus_label.assertEqual(label)
            # test system values
            _ret, sys_label = self.run_command('lsblk -d -no LABEL %s' % block_fs_dev)
            self.assertEqual(sys_label, label)

        # create a tempdir
        tmp = tempfile.mkdtemp()
        self.addCleanup(self._rmtree, tmp)

        # configuration items as arrays of dbus.Byte
        mnt = self.str_to_ay(tmp)
        fstype = self.str_to_ay(self._fs_name if self._fs_name else self._fs_signature)
        opts = self.str_to_ay('ro')

        # set the new configuration
        conf_items = {'dir': mnt, 'type': fstype, 'opts': opts, 'freq': 0, 'passno': 0}
        if label:
            conf_items['fsname'] = self.str_to_ay(fstab_label_str)
        elif disk_dev_path != block_fs_dev:
            # avoid using IDs for partitioned block devices
            conf_items['fsname'] = self.str_to_ay(block_fs_dev)
        conf = dbus.Dictionary(conf_items, signature=dbus.Signature('sv'))
        block_fs.AddConfigurationItem(('fstab', conf), self.no_options,
                                      dbus_interface=self.iface_prefix + '.Block')

        # mount using fstab options
        d = dbus.Dictionary(signature='sv')
        if self._fs_name:
            d['fstype'] = self._fs_name
        block_fs.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, tmp)
        self.addCleanup(self.try_unmount, block_fs_dev)
        self.addCleanup(self.try_unmount, disk_dev_path)

        # dbus mountpoint
        dbus_mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        # in case of a fstab record mismatch UDisks will create a dynamic mountpoint instead
        dbus_mnt = self.ay_to_str(dbus_mounts.value[0])  # mountpoints are arrays of bytes

        _ret, out = self.run_command('mount | grep %s' % block_fs_dev)
        if mount_should_fail:
            self.assertNotEqual(dbus_mnt, tmp)
            # user mountpoint
            self.assertFalse(os.path.ismount(tmp))
            self.assertNotIn(tmp, out)
        else:
            self.assertEqual(dbus_mnt, tmp)
            # system mountpoint
            self.assertTrue(os.path.ismount(tmp))
            self.assertIn(tmp, out)
            self.assertIn('ro', out)

        block_fs.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_fstab(self):
        disk_obj_path = '/block_devices/' + os.path.basename(self.vdevs[0])
        self._test_fstab_label(disk_obj_path, None, None, False)

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_fstab_complex_label(self):
        """ Test fstab mounts identified by a complex label"""

        if self._fs_signature == "xfs":
            label = '\'UD"SKS2\''
        else:
            label = '\'UD "SK S2\''
        disk_obj_path = '/block_devices/' + os.path.basename(self.vdevs[0])
        self._test_fstab_label(disk_obj_path, label, "LABEL='%s'" % label, False)

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_fstab_complex_label2(self):
        if self._fs_signature == "xfs":
            label = '"UD\'SK"'
        else:
            label = '"UD \'SK"'
        disk_obj_path = '/block_devices/' + os.path.basename(self.vdevs[0])
        self._test_fstab_label(disk_obj_path, label, 'LABEL="%s"' % label, False)

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_fstab_complex_label_bad(self):
        """ Test fstab mount mismatch by a badly escaped complex label"""

        if self._fs_signature == "xfs":
            label = '\'UD"SKS2\''
        else:
            label = '\'UD "SK S2\''
        disk_obj_path = '/block_devices/' + os.path.basename(self.vdevs[0])
        self._test_fstab_label(disk_obj_path, label, "LABEL=%s" % label, True)


    def _remove_partition(self, part):
        try:
            part.Delete(self.no_options, dbus_interface=self.iface_prefix + '.Partition')
        except:
            pass

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_fstab_partlabel(self):
        """ Test fstab mounts identified by PARTLABEL"""

        start = 1024**2
        size = 350*1024**2  # btrfs needs at least 114 MB, nilfs needs 134 MB, xfs needs 300 MB
        partlabel = '\'PRT "x LBL\''
        if self._fs_signature == "xfs":
            fslabel = '\'UD"SKS2\''
        else:
            fslabel = '\'UD "SK S2\''

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)
        disk.Format('gpt', self.no_options, dbus_interface=self.iface_prefix + '.Block')
        part = disk.CreatePartition(dbus.UInt64(start), dbus.UInt64(size), '', partlabel,
                                    self.no_options,
                                    dbus_interface=self.iface_prefix + '.PartitionTable')
        self.addCleanup(self._remove_partition, part)

        self._test_fstab_label(part, fslabel, "PARTLABEL='%s'" % partlabel, False)

    def test_unmount_no_race_in_mount_points(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # not mounted
        mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        mounts.assertLen(0)

        # mount
        d = dbus.Dictionary(signature='sv')
        d['options'] = 'ro'
        if self._fs_name:
            d['fstype'] = self._fs_name
        mnt_path = block_fs.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, block_fs_dev)
        self.addCleanup(self.try_unmount, self.vdevs[0])

        # dbus mountpoint
        dbus_mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        dbus_mnt = self.ay_to_str(dbus_mounts.value[0])  # mountpoints are arrays of bytes
        self.assertEqual(dbus_mnt, mnt_path)

        # umount and check that mount-points is immediately empty
        block_fs.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        mounts_after_unmount = self.get_property_raw(block_fs, '.Filesystem', 'MountPoints')
        self.assertEqual(len(mounts_after_unmount), 0)

    def test_userspace_mount_options(self):
        libmount_version = self._get_libmount_version()
        if libmount_version < Version('2.30'):
            self.skipTest('userspace mount options are not supported with libmount < 2.30')

        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # get real block object for the newly created filesystem
        block_fs, block_fs_dev = self._get_formatted_block_object(self.vdevs[0])
        self.assertIsNotNone(block_fs)
        self.assertIsNotNone(block_fs_dev)

        # not mounted
        mounts = self.get_property(block_fs, '.Filesystem', 'MountPoints')
        mounts.assertLen(0)

        # mount
        d = dbus.Dictionary(signature='sv')
        d['options'] = 'ro,x-test.op1'
        if self._fs_name:
            d['fstype'] = self._fs_name
        mnt_path = block_fs.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, block_fs_dev)
        self.addCleanup(self.try_unmount, self.vdevs[0])

        # check utab
        utab_opts = self.get_property(block_fs, '.Block', 'UserspaceMountOptions')
        self.assertEqual({str(o) for o in utab_opts.value},
                         {'uhelper=udisks2', 'x-test.op1'})

        # umount
        block_fs.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.assertFalse(os.path.ismount(mnt_path))

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_protective_part_overwrite(self):
        """ Test overwriting the protective partition table header by creating new filesystem on a nested partition. """
        if not self._creates_protective_part_table():
            self.skipTest('Filesystem %s does not create protective partition table' % self._fs_signature)
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)
        self.addCleanup(self.wipe_fs, self.vdevs[0])
        if self._fs_signature == "vfat":
            # need to force creation of a protective MBR
            ret, _out = self.run_command('mkfs.vfat --mbr=y %s' % self.vdevs[0])
            self.assertEqual(ret, 0)
            time.sleep(2)
        else:
           self._create_format(disk)

        # verify that there's a partition table and partition with the expected filesystem
        pttype = self.get_property(disk, '.PartitionTable', 'Type')
        pttype.assertNotEqual('gpt')
        parts = self.get_property(disk, '.PartitionTable', 'Partitions')
        parts.assertLen(1)

        part = self.get_object(parts.value[0])
        self.assertIsNotNone(part)
        part_offset = self.get_property(part, '.Partition', 'Offset')
        # expect a partition starting with offset 0
        part_offset.assertEqual(0)

        # attempt to create another filesystem on the partition
        msg = 'This partition cannot be modified because it contains a partition table; please reinitialize layout of the whole device.'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            part.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_protective_part_overwrite_mounted(self):
        """ Test overwriting the master block device carrying a protective partition table having the nested partition mounted. """
        if not self._creates_protective_part_table():
            self.skipTest('Filesystem %s does not create protective partition table' % self._fs_signature)
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)
        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)
        self.addCleanup(self.wipe_fs, self.vdevs[0])
        if self._fs_signature == "vfat":
            # need to force creation of a protective MBR
            ret, _out = self.run_command('mkfs.vfat --mbr=y %s' % self.vdevs[0])
            self.assertEqual(ret, 0)
            time.sleep(2)
        else:
           self._create_format(disk)

        # verify that there's a partition table and partition with the expected filesystem
        pttype = self.get_property(disk, '.PartitionTable', 'Type')
        pttype.assertNotEqual('gpt')
        parts = self.get_property(disk, '.PartitionTable', 'Partitions')
        parts.assertLen(1)

        part = self.get_object(parts.value[0])
        self.assertIsNotNone(part)
        part_offset = self.get_property(part, '.Partition', 'Offset')
        # expect a partition starting with offset 0
        part_offset.assertEqual(0)

        # mount the filesystem
        d = dbus.Dictionary(signature='sv')
        d['options'] = 'ro'
        if self._fs_name:
            d['fstype'] = self._fs_name
        mnt_path = part.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, mnt_path)

        # now try formatting the master block device
        msg = r"Error wiping device:.*Failed to open the device|Error synchronizing after initial wipe: Timed out waiting for object"
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')

        part.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')


class Ext2TestCase(UdisksFSTestCase):
    _fs_signature = 'ext2'
    _can_create = True and UdisksFSTestCase.command_exists('mke2fs')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('tune2fs')
    _can_mount = True
    _can_query_size = True

    def _invalid_label(self, disk):
        label = 'a' * 17  # at most 16 characters, longer should be truncated
        msg = 'org.freedesktop.UDisks2.Error.Failed: Label for ext filesystem must be at most 16 characters long.'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')


class Ext3TestCase(Ext2TestCase):
    _fs_signature = 'ext3'

    def _invalid_label(self, disk):
        pass


class Ext4TestCase(Ext2TestCase):
    _fs_signature = 'ext4'

    def _invalid_label(self, disk):
        pass

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_take_ownership(self):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create filesystem
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        # create user for our test
        self.addCleanup(self._remove_user, self.username)
        uid, gid = self._add_user(self.username)

        # mount the device
        mnt_path = disk.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, self.vdevs[0])

        # change owner of the mountpoint to our user
        os.chown(mnt_path, int(uid), int(gid))

        # now take ownership of the filesystem -- owner should now be root
        disk.TakeOwnership(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

        sys_stat = os.stat(mnt_path)
        self.assertEqual(sys_stat.st_uid, 0)
        self.assertEqual(sys_stat.st_gid, 0)

        # change the owner back and create some files and directories there
        os.chown(mnt_path, int(uid), int(gid))

        dirname = 'udisks_test_dir'
        fname = 'file.txt'
        symlinkname = 'symlink'
        fd, symlinktarget = tempfile.mkstemp(dir='/tmp')
        self.addCleanup(self.remove_file, symlinktarget, True)
        os.close(fd)
        symlinkdirname = 'symlinkdir'
        symlinkdirtarget = tempfile.mkdtemp(dir='/tmp')
        self.addCleanup(self._rmtree, symlinkdirtarget)

        os.mknod(os.path.join(mnt_path, fname))
        os.chown(os.path.join(mnt_path, fname), int(uid), int(gid))
        os.mkdir(os.path.join(mnt_path, dirname))
        os.chown(os.path.join(mnt_path, dirname), int(uid), int(gid))
        os.mknod(os.path.join(mnt_path, dirname, fname))
        os.chown(os.path.join(mnt_path, dirname, fname), int(uid), int(gid))
        os.symlink(symlinktarget, os.path.join(mnt_path, dirname, symlinkname))
        os.chown(symlinktarget, int(uid), int(gid))
        os.symlink(symlinkdirtarget, os.path.join(mnt_path, dirname, symlinkdirname))
        os.chown(symlinkdirtarget, int(uid), int(gid))
        os.mknod(os.path.join(symlinkdirtarget, fname))
        os.chown(os.path.join(symlinkdirtarget, fname), int(uid), int(gid))

        # now take ownership of the filesystem with recursive option -- owner
        # of everything should now be root
        d = dbus.Dictionary(signature='sv')
        d['recursive'] = True
        disk.TakeOwnership(d, dbus_interface=self.iface_prefix + '.Filesystem')

        sys_stat = os.stat(mnt_path)
        self.assertEqual(sys_stat.st_uid, 0)
        self.assertEqual(sys_stat.st_gid, 0)

        sys_stat = os.stat(os.path.join(mnt_path, dirname))
        self.assertEqual(sys_stat.st_uid, 0)
        self.assertEqual(sys_stat.st_gid, 0)

        sys_stat = os.stat(os.path.join(mnt_path, dirname, fname))
        self.assertEqual(sys_stat.st_uid, 0)
        self.assertEqual(sys_stat.st_gid, 0)

        # symlink target should be left untouched
        sys_stat = os.stat(symlinktarget)
        self.assertEqual(sys_stat.st_uid, int(uid))
        self.assertEqual(sys_stat.st_gid, int(gid))

        sys_stat = os.stat(os.path.join(mnt_path, dirname, symlinkname))
        self.assertEqual(sys_stat.st_uid, int(uid))
        self.assertEqual(sys_stat.st_gid, int(gid))

        sys_stat = os.lstat(os.path.join(mnt_path, dirname, symlinkname))
        self.assertEqual(sys_stat.st_uid, 0)
        self.assertEqual(sys_stat.st_gid, 0)

        # symlink target directory and its files should be left untouched
        sys_stat = os.stat(symlinkdirtarget)
        self.assertEqual(sys_stat.st_uid, int(uid))
        self.assertEqual(sys_stat.st_gid, int(gid))

        sys_stat = os.stat(os.path.join(mnt_path, dirname, symlinkdirname))
        self.assertEqual(sys_stat.st_uid, int(uid))
        self.assertEqual(sys_stat.st_gid, int(gid))

        sys_stat = os.lstat(os.path.join(mnt_path, dirname, symlinkdirname))
        self.assertEqual(sys_stat.st_uid, 0)
        self.assertEqual(sys_stat.st_gid, 0)

        sys_stat = os.stat(os.path.join(symlinkdirtarget, fname))
        self.assertEqual(sys_stat.st_uid, int(uid))
        self.assertEqual(sys_stat.st_gid, int(gid))


class XFSTestCase(UdisksFSTestCase):
    _fs_signature = 'xfs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.xfs')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('xfs_admin')
    _can_mount = True
    _can_query_size = True

    def _invalid_label(self, disk):
        label = 'a a'  # space not allowed
        msg = 'org.freedesktop.UDisks2.Error.Failed: Label for XFS filesystem cannot contain spaces.'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')


class NonPOSIXTestCase(UdisksFSTestCase):
    """ Filesystems that don't support POSIX/native file ownership
    """

    def _set_fstab_mountpoint(self, disk, options):
        # create a tempdir
        tmp = tempfile.mkdtemp()
        self.addCleanup(self._rmtree, tmp)

        # configuration items as arrays of dbus.Byte
        mnt = self.str_to_ay(tmp)
        fstype = self.str_to_ay(self._fs_name if self._fs_name else self._fs_signature)
        if options:
            opts = self.str_to_ay(options)
        else:
            opts = self.str_to_ay('defaults')

        # set the new configuration
        conf = dbus.Dictionary({'dir': mnt, 'type': fstype, 'opts': opts, 'freq': 0, 'passno': 0},
                               signature=dbus.Signature('sv'))
        disk.AddConfigurationItem(('fstab', conf), self.no_options,
                                  dbus_interface=self.iface_prefix + '.Block')

    def _mount_as_user_fstab(self, pipe, uid, gid, device):
        """ Try to mount and then unmount @device as user with given @uid and
            @gid.
            @device should be listed in /etc/fstab with proper options so user
            is able to run these operations and this shouldn't fail.
        """
        os.setresgid(gid, gid, gid)
        os.setresuid(uid, uid, uid)

        # try to mount the device
        try:
            safe_dbus.call_sync(self.iface_prefix,
                                self.path_prefix + '/block_devices/' + os.path.basename(device),
                                self.iface_prefix + '.Filesystem',
                                'Mount',
                                GLib.Variant('(a{sv})', ({},)))
        except Exception as e:
            pipe.send([False, 'Mount DBus call failed: %s' % str(e)])
            pipe.close()
            return

        ret, out = self.run_command('grep \"%s\" /proc/mounts' % device)
        if ret != 0:
            pipe.send([False, '%s not mounted' % device])
            pipe.close()
            return

        if 'uid=%s,gid=%s' % (uid, gid) not in out:
            pipe.send([False, '%s not mounted with given uid/gid.\nMount info: %s' % (device, out)])
            pipe.close()
            return

        pipe.send([True, ''])
        pipe.close()
        return

    def _mount_as_user_fstab_fail(self, pipe, uid, gid, device):
        """ Try to mount @device as user with given @uid and @gid.
            @device shouldn't be listed in /etc/fstab when running this, so
            this is expected to fail.
        """
        os.setresgid(gid, gid, gid)
        os.setresuid(uid, uid, uid)

        # try to mount the device -- it should fail
        try:
            safe_dbus.call_sync(self.iface_prefix,
                                self.path_prefix + '/block_devices/' + os.path.basename(device),
                                self.iface_prefix + '.Filesystem',
                                'Mount',
                                GLib.Variant('(a{sv})', ({},)))
        except Exception as e:
            msg = 'GDBus.Error:org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain: Not authorized to perform operation'
            if msg in str(e):
                pipe.send([True, ''])
                pipe.close()
                return
            else:
                pipe.send([False, 'Mount DBus call failed with unexpected exception: %s' % str(e)])
                pipe.close()
                return

        ret, _out = self.run_command('grep \"%s\" /proc/mounts' % device)
        if ret == 0:
            pipe.send([False, '%s was mounted for UID %d without proper record in fstab' % (device, uid)])
            pipe.close()
            return
        else:
            pipe.send([False, 'Mount DBus call didn\'t fail but %s doesn\'t seem to be mounted.' % device])
            pipe.close()
            return

    def _unmount_as_user_fstab(self, pipe, uid, gid, device):
        """ Try to unmount @device as user with given @uid and @gid.
            @device should be listed in /etc/fstab with "users" option when running this, so
            this is expected to succeed.
        """
        os.setresgid(gid, gid, gid)
        os.setresuid(uid, uid, uid)

        # try to unmount the device
        try:
            safe_dbus.call_sync(self.iface_prefix,
                                self.path_prefix + '/block_devices/' + os.path.basename(device),
                                self.iface_prefix + '.Filesystem',
                                'Unmount',
                                GLib.Variant('(a{sv})', ({},)))
        except Exception as e:
            pipe.send([False, 'Unmount DBus call failed: %s' % str(e)])
            pipe.close()
            return

        ret, _out = self.run_command('grep \"%s\" /proc/mounts' % device)
        if ret == 0:
            pipe.send([False, 'Unmount DBus call didn\'t fail but %s seems to be still mounted.' % device])
            pipe.close()
            return
        else:
            pipe.send([True, ''])
            pipe.close()
            return

    def _unmount_as_user_fstab_fail(self, pipe, uid, gid, device):
        """ Try to unmount @device as user with given @uid and @gid.
            @device shouldn't be listed in /etc/fstab when running this, so
            this is expected to fail.
        """
        os.setresgid(gid, gid, gid)
        os.setresuid(uid, uid, uid)

        # try to mount the device -- it should fail
        try:
            safe_dbus.call_sync(self.iface_prefix,
                                self.path_prefix + '/block_devices/' + os.path.basename(device),
                                self.iface_prefix + '.Filesystem',
                                'Unmount',
                                GLib.Variant('(a{sv})', ({},)))
        except Exception as e:
            msg = 'GDBus.Error:org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain: Not authorized to perform operation'
            if msg in str(e):
                pipe.send([True, ''])
                pipe.close()
                return
            else:
                pipe.send([False, 'Unmount DBus call failed with unexpected exception: %s' % str(e)])
                pipe.close()
                return

        ret, _out = self.run_command('grep \"%s\" /proc/mounts' % device)
        if ret == 0:
            pipe.send([False, 'Unmount DBus call didn\'t fail but %s seems to be still mounted.' % device])
            pipe.close()
            return
        else:
            pipe.send([False, '%s was unmounted for UID %d without proper record in fstab' % (device, uid)])
            pipe.close()
            return

    def _prepare_mount_test(self, disk, fstab, fstab_options):
        if not self._can_create:
            self.skipTest('Cannot create %s filesystem' % self._fs_signature)

        if not self._can_mount:
            self.skipTest('Cannot mount %s filesystem' % self._fs_signature)

        # this test will change /etc/fstab, we might want to revert the changes after it finishes
        fstab = self.read_file('/etc/fstab')
        self.addCleanup(self.write_file, '/etc/fstab', fstab)

        # create filesystem
        disk.Format(self._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        if fstab:
            # add the disk to fstab
            self._set_fstab_mountpoint(disk, options=fstab_options)

    def _run_as_user_with_pipes(self, fn, uid, gid, device):
        # create pipe to get error (if any)
        parent_conn, child_conn = Pipe()

        proc = Process(target=fn, args=(child_conn, int(uid), int(gid), device))
        proc.start()
        res = parent_conn.recv()
        parent_conn.close()
        proc.join()

        return res

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_fstab_fail(self):
        """ Test that user can't mount device not listed in /etc/fstab """
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create user for our test
        self.addCleanup(self._remove_user, self.username)
        uid, gid = self._add_user(self.username)

        # add unmount cleanup now in case something wrong happens in the other process
        self.addCleanup(self.try_unmount, self.vdevs[0])

        # format the disk and add it to /etc/fstab
        self._prepare_mount_test(disk, False, None)

        # now try to mount the device as the user (should fail)
        res = self._run_as_user_with_pipes(self._mount_as_user_fstab_fail, uid, gid, self.vdevs[0])
        if not res[0]:
            self.fail(res[1])

        # now mount it as root and test that user can't unmount it
        mnt_path = disk.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.assertIsNotNone(mnt_path)
        self.assertTrue(os.path.ismount(mnt_path))

        # unmounting as the user should fail
        res = self._run_as_user_with_pipes(self._unmount_as_user_fstab_fail, uid, gid, self.vdevs[0])
        if not res[0]:
            self.fail(res[1])

        self.assertTrue(os.path.ismount(mnt_path))
        self.try_unmount(mnt_path)

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_fstab_users_option(self):
        """ Test that user can mount and unmount device with the "users" option in /etc/fstab """
        # users -- any user can mount the device and any user can also unmount it
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create user for our test
        self.addCleanup(self._remove_user, self.username)
        uid, gid = self._add_user(self.username)

        # format the disk and add it to /etc/fstab with the "users" option
        self._prepare_mount_test(disk, True, 'users')

        # mount the device as the new user which should succeed
        res = self._run_as_user_with_pipes(self._mount_as_user_fstab, uid, gid, self.vdevs[0])
        if not res[0]:
            self.fail(res[1])

        # same with the unmount
        res = self._run_as_user_with_pipes(self._unmount_as_user_fstab, uid, gid, self.vdevs[0])
        if not res[0]:
            self.fail(res[1])

        # now mount it as root and test that user can still unmount it
        mnt_path = disk.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.assertIsNotNone(mnt_path)
        self.assertTrue(os.path.ismount(mnt_path))

        # unmount as the user, shoul still work thanks to the "users" option
        res = self._run_as_user_with_pipes(self._unmount_as_user_fstab, uid, gid, self.vdevs[0])
        if not res[0]:
            self.fail(res[1])

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSAFE)
    def test_mount_fstab_user_option(self):
        """ Test that user can mount and unmount device with the "user" option in /etc/fstab """
        # user -- any user can mount the device, only user that mounted it (and root) can
        # unmount it

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create user for our test
        self.addCleanup(self._remove_user, self.username)
        uid, gid = self._add_user(self.username)

        # format the disk and add it to /etc/fstab with the "users" option
        self._prepare_mount_test(disk, True, 'user')

        # mount the device as the new user which should succeed
        res = self._run_as_user_with_pipes(self._mount_as_user_fstab, uid, gid, self.vdevs[0])
        if not res[0]:
            self.fail(res[1])

        # same with the unmount
        res = self._run_as_user_with_pipes(self._unmount_as_user_fstab, uid, gid, self.vdevs[0])
        if not res[0]:
            self.fail(res[1])

        # now mount it as root and test that user can't unmount it
        mnt_path = disk.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.assertIsNotNone(mnt_path)
        self.assertTrue(os.path.ismount(mnt_path))
        self.addCleanup(self.try_unmount, self.vdevs[0])

        # unmount as the user, this should fail now, because it was mounted by root
        res = self._run_as_user_with_pipes(self._unmount_as_user_fstab_fail, uid, gid, self.vdevs[0])
        if not res[0]:
            self.fail(res[1])


class VFATTestCase(NonPOSIXTestCase):
    _fs_signature = 'vfat'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.vfat')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('fatlabel')
    _can_mount = True
    _can_query_size = True and UdisksFSTestCase.command_exists('fsck.vfat')

    def _invalid_label(self, disk):
        label = 'a' * 12  # at most 11 characters
        msg = 'org.freedesktop.UDisks2.Error.Failed: Label for VFAT filesystem must be at most 11 characters long.'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.SetLabel(label, self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

    def _get_dosfstools_version(self):
        _ret, out = self.run_command("mkfs.vfat --help")
        # mkfs.fat 4.1 (2017-01-24)
        m = re.search(r"mkfs\.fat ([\d\.]+)", out)
        if not m or len(m.groups()) != 1:
            raise RuntimeError("Failed to determine dosfstools version from: %s" % out)
        return Version(m.groups()[0])

    def _creates_protective_part_table(self):
        # dosfstools >= 4.2 create fake MBR partition table
        return self._get_dosfstools_version() >= Version('4.2')

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_repair_resize_check(self):
        super(VFATTestCase, self).test_repair_resize_check()


class EXFATTestCase(NonPOSIXTestCase):
    _fs_signature = 'exfat'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.exfat')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('exfatlabel')
    _can_mount = True
    _can_query_size = True and UdisksFSTestCase.command_exists('fsck.exfat')


class NTFSTestCase(UdisksFSTestCase):
    _fs_signature = 'ntfs'
    _fs_name = 'ntfs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.ntfs')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('ntfslabel')
    _can_mount = True
    _can_query_size = True and UdisksFSTestCase.command_exists('ntfscluster')

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not cls.command_exists('ntfs-3g'):
            raise unittest.SkipTest('ntfs-3g binary not available, skipping.')

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_repair_resize_check(self):
        super(NTFSTestCase, self).test_repair_resize_check()

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_userspace_mount_options(self):
        super(NTFSTestCase, self).test_userspace_mount_options()

class NTFS3TestCase(UdisksFSTestCase):
    _fs_signature = 'ntfs'
    _fs_name = 'ntfs3'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.ntfs')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('ntfslabel')
    _can_mount = True
    _can_query_size = True and UdisksFSTestCase.command_exists('ntfscluster')
    _have_ntfs3g = UdisksFSTestCase.command_exists('ntfs-3g')

    @classmethod
    def setUpClass(cls):
        udiskstestcase.UdisksTestCase.setUpClass()
        if not BlockDev.utils_have_kernel_module('ntfs3'):
            raise unittest.SkipTest('ntfs3 kernel module not available, skipping.')

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_repair_resize_check(self):
        super(NTFS3TestCase, self).test_repair_resize_check()

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_userspace_mount_options(self):
        super(NTFS3TestCase, self).test_userspace_mount_options()

class NTFSCommonTestCase(UdisksFSTestCase):
    _fs_signature = 'ntfs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.ntfs')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('ntfslabel')
    _can_mount = True

    def test_mount_auto_configurable_mount_options(self):
        raise unittest.SkipTest('Not applicable for the common NTFS test case, skipping.')

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_repair_resize_check(self):
        super(NTFSCommonTestCase, self).test_repair_resize_check()

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_userspace_mount_options(self):
        super(NTFSCommonTestCase, self).test_userspace_mount_options()

class BTRFSTestCase(UdisksFSTestCase):
    _fs_signature = 'btrfs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.btrfs')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('btrfs')
    _can_mount = True


class NILFS2TestCase(UdisksFSTestCase):
    _fs_signature = 'nilfs2'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.nilfs2')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('nilfs-tune')
    _can_mount = True and udiskstestcase.UdisksTestCase.module_available('nilfs2')

    @udiskstestcase.tag_test(udiskstestcase.TestTags.UNSTABLE)
    def test_userspace_mount_options(self):
        super(NILFS2TestCase, self).test_userspace_mount_options()


class F2FSTestCase(UdisksFSTestCase):
    _fs_signature = 'f2fs'
    _can_create = True and UdisksFSTestCase.command_exists('mkfs.f2fs')
    _can_label = True
    _can_relabel = False
    _can_mount = True and udiskstestcase.UdisksTestCase.module_available('f2fs')
    _can_query_size = True and UdisksFSTestCase.command_exists('dump.f2fs')

class UDFTestCase(UdisksFSTestCase):
    _fs_signature = 'udf'
    _can_create = True and UdisksFSTestCase.command_exists('mkudffs')
    _can_label = True
    _can_relabel = True and UdisksFSTestCase.command_exists('udflabel')
    _can_mount = True and udiskstestcase.UdisksTestCase.module_available('udf')

    def _get_mkudffs_version(self):
        """ Detect mkudffs version, fall back to zero in case of failure. """
        _ret, out = self.run_command('mkudffs 2>&1 | grep "mkudffs from udftools"')
        m = re.search(r'from udftools ([\d\.]+)', out)
        if not m or len(m.groups()) != 1:
            return Version("0")
        return Version(m.groups()[0])

    def _creates_protective_part_table(self):
        # udftools >= 2.0 create fake MBR partition table
        return self._get_mkudffs_version() >= Version('2.0')


class FailsystemTestCase(UdisksFSTestCase):
    # test that not supported operations fail 'nicely'

    def test_create_format(self):
        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # try to create some nonexisting filesystem
        msg = 'org.freedesktop.UDisks2.Error.NotSupported: Filesystem \'definitely-nonexisting-fs\' is not supported.'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.Format('definitely-nonexisting-fs', self.no_options,
                        dbus_interface=self.iface_prefix + '.Block')

    def test_relabel(self):
        # we need some filesystem that doesn't support setting label after creating it
        fs = F2FSTestCase

        if not fs._can_create:
            self.skipTest('Cannot create %s filesystem to test not supported '
                          'labelling.' % fs._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        # create minix filesystem without label and try to set it later
        disk.Format(fs._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])

        msg = "org.freedesktop.UDisks2.Error.Failed: Setting the label of filesystem 'f2fs' is not supported."
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.SetLabel('test', self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

    def test_mount_auto(self):
        # we need some mountable filesystem, ext4 should do the trick
        fs = Ext4TestCase

        if not fs._can_create:
            self.skipTest('Cannot create %s filesystem to test not supported '
                          'mount options.' % fs._fs_signature)

        disk = self.get_object('/block_devices/' + os.path.basename(self.vdevs[0]))
        self.assertIsNotNone(disk)

        disk.Format(fs._fs_signature, self.no_options, dbus_interface=self.iface_prefix + '.Block')
        self.addCleanup(self.wipe_fs, self.vdevs[0])
        self.addCleanup(self.try_unmount, self.vdevs[0])  # paranoid cleanup

        # wrong fstype
        d = dbus.Dictionary(signature='sv')
        d['fstype'] = 'xfs'

        msg = '[Ww]rong fs type'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            mnt_path = disk.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
            self.assertIsNone(mnt_path)

        # invalid option
        d = dbus.Dictionary(signature='sv')
        d['options'] = 'definitely-nonexisting-option'

        msg = 'org.freedesktop.UDisks2.Error.OptionNotPermitted: Mount option '\
              '`definitely-nonexisting-option\' is not allowed'
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            mnt_path = disk.Mount(d, dbus_interface=self.iface_prefix + '.Filesystem')
            self.assertIsNone(mnt_path)

        # should not be mounted -- so lets try to unmount it
        msg = 'org.freedesktop.UDisks2.Error.NotMounted: Device `%s\' is not '\
              'mounted' % self.vdevs[0]
        with six.assertRaisesRegex(self, dbus.exceptions.DBusException, msg):
            disk.Unmount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')

    def test_mount_fstab(self):
        pass

    def test_size(self):
        pass

class UdisksISO9660TestCase(udiskstestcase.UdisksTestCase):
    def _create_iso9660_on_dev(self, dev):
        if not shutil.which("genisoimage"):
            self.skipTest("Cannot create an iso9660 file system")

        tmp = tempfile.mkdtemp()
        try:
            with open(os.path.join(tmp, "test_file"), "w") as f:
                f.write("TEST\n")
            ret, _out = self.run_command("genisoimage -V TEST_iso9660 -o %s %s" % (dev, tmp))
            self.assertEqual(ret, 0)
            self.udev_settle()
            # give udisks chance to probe the filesystem
            time.sleep(1)
        finally:
            shutil.rmtree(tmp)

    def test_mount_auto(self):
        dev = self.vdevs[0]

        # create an iso9660 FS on the device
        self._create_iso9660_on_dev(dev)
        self.addCleanup(self.wipe_fs, dev)

        disk = self.get_object('/block_devices/' + os.path.basename(dev))
        self.assertIsNotNone(disk)

        self.assertHasIface(disk, self.iface_prefix + '.Filesystem')

        # mount
        disk.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, dev)

        # check dbus mountpoint
        dbus_mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        mnt = self.ay_to_str(dbus_mounts.value[0])
        self.assertStartswith(mnt.split("/")[-1], "TEST_iso9660")
        self.assertTrue(os.path.ismount(mnt))

        # default modes should be used
        _ret, out = self.run_command('mount | grep %s' % dev)
        self.assertIn(mnt, out)
        if "mode=0" in out:
            # old way mode and dmode are reported
            self.assertIn("mode=0400", out)
            self.assertIn("dmode=0500", out)
        else:
            self.assertIn("fmode=400", out)
            self.assertIn("dmode=500", out)

    def test_mount_user_opts(self):
        dev = self.vdevs[0]

        # create an iso9660 FS on the device
        self._create_iso9660_on_dev(dev)
        self.addCleanup(self.wipe_fs, dev)

        disk = self.get_object('/block_devices/' + os.path.basename(dev))
        self.assertIsNotNone(disk)

        # mount with specific mode/dmode
        extra = dbus.Dictionary(signature='sv')
        extra['options'] = 'mode=0640,dmode=0550'

        self.assertHasIface(disk, self.iface_prefix + '.Filesystem')

        # mount
        disk.Mount(extra, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, dev)

        # check dbus mountpoint
        dbus_mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        mnt = self.ay_to_str(dbus_mounts.value[0])
        self.assertStartswith(mnt.split("/")[-1], "TEST_iso9660")
        self.assertTrue(os.path.ismount(mnt))

        # specified modes should be used
        _ret, out = self.run_command('mount | grep %s' % dev)
        self.assertIn(mnt, out)
        if "mode=0" in out:
            # old way mode and dmode are reported
            self.assertIn("mode=0640", out)
            self.assertIn("dmode=0550", out)
        else:
            self.assertIn("fmode=640", out)
            self.assertIn("dmode=550", out)

    def test_mount_shared(self):
        dev = self.vdevs[0]

        # create an iso9660 FS on the device
        self._create_iso9660_on_dev(dev)
        self.addCleanup(self.wipe_fs, dev)

        disk = self.get_object('/block_devices/' + os.path.basename(dev))
        self.assertIsNotNone(disk)

        self.set_udev_properties(dev, { "UDISKS_FILESYSTEM_SHARED": "1" })
        self.addCleanup(self.set_udev_properties, dev, None)

        self.assertHasIface(disk, self.iface_prefix + '.Filesystem')

        # mount
        disk.Mount(self.no_options, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, dev)

        # check dbus mountpoint
        dbus_mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        mnt = self.ay_to_str(dbus_mounts.value[0])
        self.assertStartswith(mnt.split("/")[-1], "TEST_iso9660")
        self.assertTrue(os.path.ismount(mnt))

        # modes for shared mounts should be used
        _ret, out = self.run_command('mount | grep %s' % dev)
        self.assertIn(mnt, out)
        if "mode=0" in out:
            # old way mode and dmode are reported
            self.assertIn("mode=0444", out)
            self.assertIn("dmode=0555", out)
        else:
            self.assertIn("fmode=444", out)
            self.assertIn("dmode=555", out)

    def test_mount_shared_custom(self):
        dev = self.vdevs[0]

        # create an iso9660 FS on the device
        self._create_iso9660_on_dev(dev)
        self.addCleanup(self.wipe_fs, dev)

        disk = self.get_object('/block_devices/' + os.path.basename(dev))
        self.assertIsNotNone(disk)

        self.set_udev_properties(dev, { "UDISKS_FILESYSTEM_SHARED": "1" })
        self.addCleanup(self.set_udev_properties, dev, None)

        # mount with specific mode/dmode
        extra = dbus.Dictionary(signature='sv')
        extra['options'] = 'mode=0600,dmode=0500'

        self.assertHasIface(disk, self.iface_prefix + '.Filesystem')

        disk.Mount(extra, dbus_interface=self.iface_prefix + '.Filesystem')
        self.addCleanup(self.try_unmount, dev)

        # check dbus mountpoint
        dbus_mounts = self.get_property(disk, '.Filesystem', 'MountPoints')
        dbus_mounts.assertLen(1)  # just one mountpoint
        mnt = self.ay_to_str(dbus_mounts.value[0])
        self.assertStartswith(mnt.split("/")[-1], "TEST_iso9660")
        self.assertTrue(os.path.ismount(mnt))

        # the specified modes should be used even for a shared mount
        _ret, out = self.run_command('mount | grep %s' % dev)
        self.assertIn(mnt, out)
        if "mode=0" in out:
            # old way mode and dmode are reported
            self.assertIn("mode=0600", out)
            self.assertIn("dmode=0500", out)
        else:
            self.assertIn("fmode=600", out)
            self.assertIn("dmode=500", out)

del UdisksFSTestCase  # skip UdisksFSTestCase
