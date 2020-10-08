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
import pdb
import re
import six
import atexit
import traceback
import yaml
from datetime import datetime


SKIP_CONFIG = 'skip.yml'


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


def install_config_files(projdir, tmpdir):
    """
    Copies DBus, PolicyKit and UDev config file(s)

    Returns a list of files that need to be restored or deleted.
    """
    copied = []

    # udev rules
    tgtdir = next((d for d in ['/usr/lib/udev/rules.d/', '/lib/udev/rules.d'] if os.path.exists(d)), None)
    if tgtdir is None:
        raise RuntimeError('Cannot find udev rules directory')

    copied.extend(_copy_files((os.path.join(projdir, 'data/80-udisks2.rules'),),
                              tgtdir, tmpdir))

    # dbus config files
    copied.extend(_copy_files((os.path.join(projdir, 'data/org.freedesktop.UDisks2.conf'),),
                              '/usr/share/dbus-1/system.d/', tmpdir))

    # polkit policies
    policies = glob.glob(projdir + '/data/*.policy') + glob.glob(projdir + '/modules/*/data/*.policy')
    copied.extend(_copy_files(policies, '/usr/share/polkit-1/actions/', tmpdir))

    # udisks2.conf
    if not os.path.exists('/etc/udisks2'):
        os.mkdir('/etc/udisks2', 0o755)
        os.mkdir('/etc/udisks2/modules.conf.d', 0o755)
    copied.extend(_copy_files((os.path.join(projdir, 'udisks/udisks2.conf'),),
                              '/etc/udisks2/', tmpdir))
    copied.extend(_copy_files((os.path.join(projdir, 'udisks/mount_options.conf'),),
                              '/etc/udisks2/', tmpdir))

    # ZRAM module supplemental files
    if os.path.exists(os.path.join(projdir, 'modules/zram/data/udisks2-zram-setup@.service')):
        copied.extend(_copy_files((os.path.join(projdir, 'modules/zram/data/90-udisks2-zram.rules'),),
                                  tgtdir, tmpdir))
        copied.extend(_copy_files((os.path.join(projdir, 'modules/zram/data/udisks2-zram-setup@.service'),),
                                  '/usr/lib/systemd/system/', tmpdir))

    return copied

def restore_files(restore_list, tmpdir):
    for f, delete in restore_list:
        if delete:
            print(f)
            os.unlink(f)
        else:
            shutil.move(os.path.join(tmpdir, os.path.basename(f)), f)


def udev_shake():
    assert subprocess.call(['udevadm', 'control', '--reload']) == 0
    assert subprocess.call(['udevadm', 'trigger']) == 0
    assert subprocess.call(['udevadm', 'settle']) == 0


def _get_tests_from_suite(suite, tests):
    """ Extract tests from the test suite """
    # 'tests' we get from 'unittest.defaultTestLoader.discover' are "wrapped"
    # in multiple 'unittest.suite.TestSuite' classes/lists so we need to "unpack"
    # the indivudual test cases
    for test in suite:
        if isinstance(test, unittest.suite.TestSuite):
            _get_tests_from_suite(test, tests)

        if isinstance(test, unittest.TestCase):
            tests.append(test)

    return tests


def _get_test_tags(test):
    """ Get test tags for single test case """

    tags = set()

    # test failed to load, usually some ImportError or something really broken
    # in the test file, just return empty list and let it fail
    # with python2 the loader will raise an exception directly without returning
    # a "fake" FailedTest test case
    if six.PY3 and isinstance(test, unittest.loader._FailedTest):
        return tags

    test_fn = getattr(test, test._testMethodName)

    # it is possible to either tag a test funcion or the class so we need to
    # check both for the tag
    if getattr(test_fn, "slow", False) or getattr(test_fn.__self__, "slow", False):
        tags.add(udiskstestcase.TestTags.SLOW)
    if getattr(test_fn, "unstable", False) or getattr(test_fn.__self__, "unstable", False):
        tags.add(udiskstestcase.TestTags.UNSTABLE)
    if getattr(test_fn, "unsafe", False) or getattr(test_fn.__self__, "unsafe", False):
        tags.add(udiskstestcase.TestTags.UNSAFE)
    if getattr(test_fn, "nostorage", False) or getattr(test_fn.__self__, "nostorage", False):
        tags.add(udiskstestcase.TestTags.NOSTORAGE)
    if getattr(test_fn, "extradeps", False) or getattr(test_fn.__self__, "extradeps", False):
        tags.add(udiskstestcase.TestTags.EXTRADEPS)
    if getattr(test_fn, "loadtest", False) or getattr(test_fn.__self__, "loadtest", False):
        tags.add(udiskstestcase.TestTags.LOADTEST)

    tags.add(udiskstestcase.TestTags.ALL)

    return tags


def _should_skip(distro=None, version=None, arch=None, reason=None):
    # all these can be lists or a single value, so covert everything to list
    if distro is not None and type(distro) is not list:
        distro = [distro]
    if version is not None and type(version) is not list:
        version = [version]
    if arch is not None and type(arch) is not list:
        arch = [arch]

    # DISTRO, VERSION and ARCH variables are set in main, we don't need to
    # call hostnamectl etc. for every test run
    if (distro is None or DISTRO in distro) and (version is None or VERSION in version) and \
       (arch is None or ARCH in arch):
        return True


def _parse_skip_config(config):
    skipped_tests = dict()

    if not os.path.isfile(config):
        print("File with list of tests to skip not found.")
        return skipped_tests

    with open(config) as f:
        data = f.read()
    parsed = yaml.load(data, Loader=yaml.SafeLoader)

    for entry in parsed:
        for skip in entry["skip_on"]:
            if _should_skip(**skip):
                skipped_tests[entry["test"]] = skip["reason"]

    return skipped_tests


def _split_test_id(test_id):
    # test.id() looks like 'crypto_test.CryptoTestResize.test_luks2_resize'
    # and we want to print 'test_luks2_resize (crypto_test.CryptoTestResize)'
    test_desc = test.id().split(".")
    test_name = test_desc[-1]
    test_module = ".".join(test_desc[:-1])

    return test_name, test_module


def _print_skip_message(test, skip_tags, missing):
    test_id = test.id()
    test_module, test_name = _split_test_id(test_id)

    if missing:
        reason = 'skipping test because it is not tagged as one of: ' + ', '.join((t.value for t in skip_tags))
    else:
        reason = 'skipping test because it is tagged as: ' + ', '.join((t.value for t in skip_tags))

    if test._testMethodDoc:
        print("%s (%s)\n%s ... skipped '%s'" % (test_name, test_module, test._testMethodDoc, reason),
              file=sys.stderr)
    else:
        print("%s (%s) ... skipped '%s'" % (test_name, test_module, reason),
              file=sys.stderr)


class DebugTestResult(unittest.TextTestResult):

    def addError(self, test, err):
        traceback.print_exception(*err)
        pdb.post_mortem(err[2])
        super(DebugTestResult, self).addError(test, err)

    def addFailure(self, test, err):
        traceback.print_exception(*err)
        pdb.post_mortem(err[2])
        super(DebugTestResult, self).addFailure(test, err)


def parse_args():
    """ Parse cmdline arguments """

    argparser = argparse.ArgumentParser(description='udisks D-Bus test suite')
    argparser.add_argument('-l', '--log-file', dest='logfile',
                           help='write daemon log to a file')
    argparser.add_argument('testname', nargs='*',
                           help='name of test class or method (e. g. "Drive", "FS.test_ext2")')
    argparser.add_argument('-s', '--system', dest='system',
                           help='run the test against the system installed instance',
                           action='store_true')
    argparser.add_argument('-f', '--failfast', dest='failfast',
                           help='stop the test run on a first error',
                           action='store_true')
    argparser.add_argument('-p', '--pdb', dest='pdb',
                           help='run pdb after a failed test',
                           action='store_true')
    argparser.add_argument('--exclude-tags', nargs='+', dest='exclude_tags',
                           help='skip tests tagged with (at least one of) the provided tags')
    argparser.add_argument('--include-tags', nargs='+', dest='include_tags',
                           help='run only tests tagged with (at least one of) the provided tags')
    argparser.add_argument('--list-tags', dest='list_tags', help='print available tags and exit',
                           action='store_true')
    args = argparser.parse_args()

    all_tags = set(udiskstestcase.TestTags.get_tags())

    if args.list_tags:
        print('Available tags:', ', '.join(all_tags))
        sys.exit(0)

    # lets convert these to sets now to make argument checks easier
    args.include_tags = set(args.include_tags) if args.include_tags else set()
    args.exclude_tags = set(args.exclude_tags) if args.exclude_tags else set()

    # make sure user provided only valid tags
    if not all_tags.issuperset(args.include_tags):
        print('Unknown tag(s) specified:', ', '.join(args.include_tags - all_tags), file=sys.stderr)
        sys.exit(1)
    if not all_tags.issuperset(args.exclude_tags):
        print('Unknown tag(s) specified:', ', '.join(args.exclude_tags - all_tags), file=sys.stderr)
        sys.exit(1)

    # for backwards compatibility we want to exclude unsafe, unstable and loadtests by default
    if not 'JENKINS_HOME' in os.environ and not (udiskstestcase.TestTags.UNSAFE.value in args.include_tags or
                                                 udiskstestcase.TestTags.ALL.value in args.include_tags):
        args.exclude_tags.add(udiskstestcase.TestTags.UNSAFE.value)
    if not (udiskstestcase.TestTags.UNSTABLE.value in args.include_tags or
            udiskstestcase.TestTags.ALL.value in args.include_tags):
        args.exclude_tags.add(udiskstestcase.TestTags.UNSTABLE.value)
    if not (udiskstestcase.TestTags.LOADTEST.value in args.include_tags):
        args.exclude_tags.add(udiskstestcase.TestTags.LOADTEST.value)

    return args


if __name__ == '__main__':
    tmpdir = None
    daemon = None
    suite = unittest.TestSuite()
    daemon_log = sys.stdout

    # store time when tests started (needed for journal cropping)
    start_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    args = parse_args()

    setup_vdevs()

    if args.logfile:
        daemon_log = open(args.logfile, mode='w')

    testdir = os.path.abspath(os.path.dirname(__file__))
    projdir = os.path.abspath(os.path.normpath(os.path.join(testdir, '..', '..', '..')))

    # use in-tree udisks tools
    if not args.system:
        if os.path.exists(os.path.join(projdir, 'tools', 'udisksctl')):
            os.environ["PATH"] = ':'.join([os.path.join(projdir, 'tools'), os.environ["PATH"]])

    if not args.system:
        # find which binary we're about to test: this also affects the D-Bus interface and object paths
        daemon_bin = find_daemon(projdir, args.system)

        tmpdir = tempfile.mkdtemp(prefix='udisks-tst-')
        atexit.register(shutil.rmtree, tmpdir)

        files_to_restore = install_config_files(projdir, tmpdir)
        atexit.register(restore_files, files_to_restore, tmpdir)

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

    loader = unittest.defaultTestLoader
    suite = unittest.TestSuite()

    os.environ["UDISKS_TESTS_ARG_SYSTEM"] = str(int(args.system))
    os.environ["UDISKS_TESTS_PROJDIR"] = projdir

    if args.testname:
        test_cases = loader.loadTestsFromNames(args.testname)
    else:
        test_cases = loader.discover(start_dir=testdir)

    # truncate the flight record file and make sure it exists
    with open(udiskstestcase.FLIGHT_RECORD_FILE, "w"):
        pass

    # extract list of test classes so we can check/run them manually one by one
    tests = []
    tests = _get_tests_from_suite(test_cases, tests)

    # get sets of include/exclude tags as tags not strings from arguments
    include_tags = set(udiskstestcase.TestTags.get_tag_by_value(t) for t in args.include_tags)
    exclude_tags = set(udiskstestcase.TestTags.get_tag_by_value(t) for t in args.exclude_tags)

    # get distro and arch here so we don't have to do this for every test
    DISTRO, VERSION = udiskstestcase.get_version()
    ARCH = os.uname()[-1]

    # get list of tests to skip from the config file
    skipping = _parse_skip_config(os.path.join(testdir, SKIP_CONFIG))

    for test in tests:
        test_id = test.id()

        # get tags and (possibly) skip the test
        tags = _get_test_tags(test)

        # if user specified include_tags, test must have at least one of these to run
        if include_tags and not (include_tags & tags):
            _print_skip_message(test, include_tags - tags, missing=True)
            continue

        # if user specified exclude_tags, test can't have any of these
        if exclude_tags and (exclude_tags & tags):
            _print_skip_message(test, exclude_tags & tags, missing=False)
            continue

        # check if the test is in the list of tests to skip
        skip_id = next((test_pattern for test_pattern in skipping.keys() if re.search(test_pattern, test_id)), None)
        if skip_id:
            test_name, test_module = _split_test_id(test_id)
            reason = "not supported on this distribution in this version and arch: %s" % skipping[skip_id]
            print("%s (%s)\n%s ... skipped '%s'" % (test_name, test_module,
                                                    test._testMethodDoc, reason),
                  file=sys.stderr)
            continue

        # finally add the test to the suite
        suite.addTest(test)

    if args.pdb:
        runner = unittest.TextTestRunner(verbosity=2, failfast=args.failfast, resultclass=DebugTestResult)
    else:
        runner = unittest.TextTestRunner(verbosity=2, failfast=args.failfast)

    result = runner.run(suite)

    if not args.system:
        daemon.terminate()
        daemon.wait()

        if args.logfile:
            daemon_log.close()

        udev_shake()

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
