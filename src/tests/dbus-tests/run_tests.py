#!/usr/bin/python3

from __future__ import print_function

import os
import sys
import time
import subprocess
import argparse
import unittest
import udiskstestcase
import glob
import shutil
import tempfile
import re
import imp
import atexit
from datetime import datetime

def find_daemon(projdir, system):
    if not system:
        if os.path.exists(os.path.join(projdir, 'src', 'udisksd')):
            daemon_bin = 'udisksd'
        else:
            print("Cannot find the daemon binary", file=sys.stderr)
            sys.exit(1)
    else:
        if os.path.exists('/usr/libexec/udisks2/udisksd'):
            daemon_bin = 'udisksd'

    return daemon_bin


def setup_vdevs():
    '''create virtual test devices'''

    orig_devs = {dev for dev in os.listdir("/dev") if re.match(r'sd[a-z]+$', dev)}

    # create fake SCSI hard drives
    assert subprocess.call(["targetcli", "restoreconfig src/tests/dbus-tests/targetcli_config.json"]) == 0

    # wait until udev fully processes all the newly created devices
    assert subprocess.call(['udevadm', 'settle']) == 0

    devs = {dev for dev in os.listdir("/dev") if re.match(r'sd[a-z]+$', dev)}

    vdevs = ["/dev/%s" % dev for dev in (devs - orig_devs)]  #pylint: disable=superfluous-parens

    # let's be 100% sure that we pick a virtual one
    for d in vdevs:
        with open('/sys/block/%s/device/model' %
                  os.path.basename(d)) as model_file:
            assert model_file.read().strip() == 'udisks_test_dis'

    udiskstestcase.test_devs = vdevs


def _copy_files(source_files, target_dir, tmpdir):
    """
    Copies the source files to the target directory.  If the file exists in the
    target dir it's backed up to tmpdir and placed on a list of files to
    restore.  If the file doesn't exist it's flagged to be deleted.
    Use restore_files for processing.

    Returns a list of files that need to be restored or deleted.
    """
    restore_list = []
    for f in source_files:
        tgt = os.path.join(target_dir, os.path.basename(f))
        if os.path.exists(tgt):
            shutil.move(tgt, tmpdir)
            restore_list.append((tgt, False))
        else:
            restore_list.append((tgt, True))

        print("Copying file: %s to %s directory!" % (f, target_dir))
        shutil.copy(f, target_dir)

    return restore_list


def install_new_policy(projdir, tmpdir):
    """
    Copies the polkit policy files.
    """
    files = glob.glob(projdir + '/data/*.policy') + \
            glob.glob(projdir + '/modules/*/data/*.policy')
    return _copy_files(files, '/usr/share/polkit-1/actions/', tmpdir)


def install_new_dbus_conf(projdir, tmpdir):
    """
    Copies the DBus config file(s)

    Returns a list of files that need to be restored or deleted.
    """
    return _copy_files([os.path.join(projdir,
                                    "data/org.freedesktop.UDisks2.conf"), ],
                       '/etc/dbus-1/system.d/',
                       tmpdir)


def install_new_udev_rules(projdir, tmpdir):
    """
    Copies the udev rules file to correct location.

    Returns a list of file(s) that need to be restored or deleted.
    """
    tgt = ""
    for p in ['/usr/lib/udev/rules.d/', '/lib/udev/rules.d']:
        if os.path.exists(p):
            tgt = p
            break

    assert tgt
    return _copy_files([os.path.join(projdir,
                                    "data/80-udisks2.rules"), ],
                       tgt,
                       tmpdir)


def restore_files(restore_list, tmpdir):
    banner = False
    for f, delete in restore_list:
        if delete:
            if not banner:
                print("*NOT* deleting the following file(s) that we placed there!")
                banner = True
            print(f)
            # os.unlink(f)
            # The other test(s) needs these files, lets leave until we get
            # this common code integrated with them as well.
            pass
        else:
            shutil.move(os.path.join(tmpdir, os.path.basename(f)), f)


def udev_shake():
    assert subprocess.call(['udevadm', 'control', '--reload']) == 0
    assert subprocess.call(['udevadm', 'trigger']) == 0
    assert subprocess.call(['udevadm', 'settle']) == 0


if __name__ == '__main__':
    files_to_restore = []
    tmpdir = None
    daemon = None
    cleaner = None
    suite = unittest.TestSuite()
    daemon_log = sys.stdout

    # store time when tests started (needed for journal cropping)
    start_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # XXX: Disable cleaning using blivet for now, it's causing issues because
    # importing blivet in the cleanup code makes the system udisks daemon
    # autostart while the cleanup removes devices wildly which causes the daemon
    # to segfault.
    skip_clean = True
    # try:
    #     imp.find_module('blivet')
    # except ImportError:
    #     skip_clean = True
    # else:
    #     skip_clean = False

    if skip_clean:
        print('Blivet not installed: Skipping force clean')
    else:
        import force_clean
        cleaner = force_clean.ForceClean()

    argparser = argparse.ArgumentParser(description='udisks D-Bus test suite')
    argparser.add_argument('-l', '--log-file', dest='logfile',
                           help='write daemon log to a file')
    argparser.add_argument('testname', nargs='*',
                           help='name of test class or method (e. g. "Drive", "FS.test_ext2")')
    argparser.add_argument('-s', '--system', dest='system',
                           help='run the test against the system installed instance',
                           action='store_true')
    args = argparser.parse_args()

    setup_vdevs()

    if args.logfile:
        daemon_log = open(args.logfile, mode='w')

    testdir = os.path.abspath(os.path.dirname(__file__))
    projdir = os.path.abspath(os.path.normpath(os.path.join(testdir, '..', '..', '..')))

    # find which binary we're about to test: this also affects the D-Bus interface and object paths
    daemon_bin = find_daemon(projdir, args.system)

    # use in-tree udisks tools
    if not args.system:
        if os.path.exists(os.path.join(projdir, 'tools', 'udisksctl')):
            os.environ["PATH"] = ':'.join([os.path.join(projdir, 'tools'), os.environ["PATH"]])

    if not skip_clean:
        cleaner.record_state()

    if not args.system:
        tmpdir = tempfile.mkdtemp(prefix='udisks-tst-')
        atexit.register(shutil.rmtree, tmpdir)
        files_to_restore.extend(install_new_policy(projdir, tmpdir))
        files_to_restore.extend(install_new_dbus_conf(projdir, tmpdir))
        files_to_restore.extend(install_new_udev_rules(projdir, tmpdir))

        udev_shake()

        daemon_bin_path = os.path.join(projdir, 'src', daemon_bin)

        # start the devel tree daemon
        daemon = subprocess.Popen([daemon_bin_path, '--replace', '--uninstalled', '--debug'],
                                  shell=False, stdout=daemon_log, stderr=daemon_log)
        # give the daemon some time to initialize
        time.sleep(3)
        daemon.poll()
        if daemon.returncode is not None:
            print("Fatal: Unable to start the daemon process", file=sys.stderr)
            sys.exit(1)
    else:
        print("Not spawning own process: testing the system installed instance.")
        time.sleep(3)

    # Load all files in this directory whose name starts with 'test'
    if args.testname:
        for n in args.testname:
            suite.addTests(unittest.TestLoader().loadTestsFromName(n))
    else:
        for test_cases in unittest.defaultTestLoader.discover(testdir):
            suite.addTest(test_cases)

    # truncate the flight record file and make sure it exists
    with open(udiskstestcase.FLIGHT_RECORD_FILE, "w"):
        pass

    result = unittest.TextTestRunner(verbosity=2).run(suite)

    if not args.system:
        daemon.terminate()
        daemon.wait()

        if args.logfile:
            daemon_log.close()

        restore_files(files_to_restore, tmpdir)

        udev_shake()

    if not skip_clean:
        cleaner.restore_state()
    else:
        # remove the fake SCSI devices and their backing files
        subprocess.call(['targetcli', 'clearconfig confirm=True'])
        for disk_file in glob.glob("/var/tmp/udisks_test_disk*"):
            os.unlink(disk_file)

    # dump cropped journal to log file
    with open('journaldump.log', "w") as outfile:
        try:
            subprocess.call(['journalctl', '-S', start_time], stdout=outfile)
        except Exception as e:
            print('Failed to save journal: %s' % str(e), file=outfile)

    if result.wasSuccessful():
        sys.exit(0)
    else:
        sys.exit(1)
