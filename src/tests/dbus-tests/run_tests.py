#!/usr/bin/python3

import os
import sys
import time
import subprocess
import argparse
import unittest
import storagedtestcase
import glob
import shutil
import tempfile


VDEV_SIZE = 300000000  # size of virtual test device


def find_daemon(projdir, system):
    if not system:
        if os.path.exists(os.path.join(projdir, 'src', 'storaged')):
            daemon_bin = 'storaged'
        elif os.path.exists(os.path.join(projdir, 'src', 'udisksd')):
            daemon_bin = 'udisksd'
        else:
            print("Cannot find the daemon binary", file=sys.stderr)
            sys.exit(1)
    else:
        if os.path.exists('/usr/libexec/storaged/storaged'):
            daemon_bin = 'storaged'
        elif os.path.exists('/usr/libexec/udisks2/udisksd'):
            daemon_bin = 'udisksd'

    return daemon_bin


def setup_vdevs():
    '''create virtual test devices'''

    # craete 4 fake SCSI hard drives
    assert subprocess.call(['modprobe', 'scsi_debug', 'dev_size_mb=%i' % (
        VDEV_SIZE / 1048576), 'num_tgts=4']) == 0, 'Failure to modprobe scsi_debug'

    # wait until the drives got created
    dirs = []
    while len(dirs) < 4:
        dirs = glob.glob('/sys/bus/pseudo/drivers/scsi_debug/adapter*/host*/target*/*:*/block')
        time.sleep(0.1)
    assert len(dirs) == 4

    vdevs = []
    for d in dirs:
        devs = os.listdir(d)
        assert len(devs) == 1
        vdevs.append('/dev/' + devs[0])
        assert os.path.exists(vdevs[-1])

    # let's be 100% sure that we pick a virtual one
    for d in vdevs:
        with open('/sys/block/%s/device/model' %
                    os.path.basename(d)) as model_file:
            assert model_file.read().strip() == 'scsi_debug'

    storagedtestcase.test_devs = vdevs


def install_new_policy(projdir, tmpdir):
    '''Copies the polkit policies to the system directory and backs up eventually the existing files.
       Returns a list of files that need to be restored.'''
    files = glob.glob(projdir + '/data/*.policy') + glob.glob(projdir + '/modules/*/data/*.policy')
    restore_list = []
    for f in files:
        tgt = '/usr/share/polkit-1/actions/' + os.path.basename(f)
        if os.path.exists(tgt):
            shutil.move(tgt, tmpdir.name)
            restore_list.append(tgt)
        shutil.copy(f, '/usr/share/polkit-1/actions/')

    return restore_list


def restore_policy(restore_list, tmpdir):
    for f in restore_list:
        shutil.move(os.path.join(tmpdir.name, os.path.basename(f)), f)


if __name__ == '__main__':
    suite = unittest.TestSuite()
    daemon_log = sys.stdout

    argparser = argparse.ArgumentParser(description='storaged D-Bus test suite')
    argparser.add_argument('-l', '--log-file', dest='logfile',
                           help='write daemon log to a file')
    argparser.add_argument('testname', nargs='*',
                           help='name of test class or method (e. g. "Drive", "FS.test_ext2")')
    argparser.add_argument('-s', '--system', dest='system',
                           help='run the test against the system installed instance',
                           action='store_true')
    args = argparser.parse_args()

    # ensure that the scsi_debug module is loaded
    if os.path.isdir('/sys/module/scsi_debug'):
        sys.stderr.write('The scsi_debug module is already loaded; please '
                         'remove before running this test.\n')
        sys.exit(1)
    setup_vdevs()

    if args.logfile:
        daemon_log = open(args.logfile, mode='w')

    testdir = os.path.abspath(os.path.dirname(__file__))
    projdir = os.path.abspath(os.path.normpath(os.path.join(testdir, '..', '..', '..')))

    # find which binary we're about to test: this also affects the D-Bus interface and object paths
    daemon_bin = find_daemon(projdir, args.system)
    storagedtestcase.daemon_bin = daemon_bin

    if not args.system:
        tmpdir = tempfile.TemporaryDirectory(prefix='storaged-tst-')
        policy_files = install_new_policy(projdir, tmpdir)

        daemon_bin_path = os.path.join(projdir, 'src', daemon_bin)

        # start the devel tree daemon
        daemon = subprocess.Popen([daemon_bin_path, '--replace', '--uninstalled',
            '--force-load-modules'], shell=False, stdout=daemon_log, stderr=daemon_log)
        # give the daemon some time to initialize
        time.sleep(3)
        daemon.poll()
        if daemon.returncode != None:
            print("Fatal: Unable to start the daemon process", file=sys.stderr)
            sys.exit(1)
    else:
        print("Not spawning own process: testing the system installed instance.")

    # Load all files in this directory whose name starts with 'test'
    if args.testname:
        for n in args.testname:
            suite.addTests(unittest.TestLoader().loadTestsFromName(n))
    else:
        for test_cases in unittest.defaultTestLoader.discover(testdir):
            suite.addTest(test_cases)
    result = unittest.TextTestRunner(verbosity=2).run(suite)

    if not args.system:
        daemon.terminate()
        daemon.wait()
        daemon_log.close()

        restore_policy(policy_files, tmpdir)
        tmpdir.cleanup()

    subprocess.call(['modprobe', '-r', 'scsi_debug'])

    if result.wasSuccessful():
        sys.exit(0)
    else:
        sys.exit(1)
