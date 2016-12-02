#!/usr/bin/python3
# (C) 2011 Sebastian Heinlein
# (C) 2012 Canonical Ltd.
# Authors:
# Sebastian Heinlein <sebi@glatzor.de>
# Martin Pitt <martin.pitt@ubuntu.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

'''Simple mock polkit daemon for test suites.

This also provides some convenience API for launching the daemon and for
writing unittest test cases involving polkit operations.
'''

import os
import argparse
import unittest
import signal
import time

import dbus
import dbus.service
from gi.repository import GLib

# ----------------------------------------------------------------------------

class TestPolicyKitDaemon(dbus.service.Object):
    def __init__(self, allowed_actions, on_bus=None):
        '''Initialize test polkit daemon.

        @allowed_actions is a list of PolicyKit action IDs which will be
        allowed (active/inactive sessions or user IDs will not be considered);
        all actions not in that list will be denied. If 'all' is an element of
        @allowed_actions, all actions will be allowed.

        When @on_bus string is given, the daemon will run on that D-BUS
        address, otherwise on the system D-BUS.
        '''
        self.allowed_actions = allowed_actions
        if on_bus:
            bus = dbus.bus.BusConnection(on_bus)
        else:
            bus = dbus.SystemBus()
        bus_name = dbus.service.BusName('org.freedesktop.PolicyKit1',
                                        bus, do_not_queue=True,
                                        replace_existing=True,
                                        allow_replacement=True)
        bus.add_signal_receiver(self.on_disconnected, signal_name='Disconnected')

        dbus.service.Object.__init__(self, bus_name,
                                     '/org/freedesktop/PolicyKit1/Authority')
        self.loop = GLib.MainLoop()

    def run(self):
        self.loop.run()

    @dbus.service.method('org.freedesktop.PolicyKit1.Authority',
                         in_signature='(sa{sv})sa{ss}us',
                         out_signature='(bba{ss})')
    def CheckAuthorization(self, subject, action_id, details, flags,
                           cancellation_id):
        if 'all' in self.allowed_actions:
            allowed = True
        else:
            allowed = action_id in self.allowed_actions
        challenged = False
        details = {'test': 'test'}
        return (allowed, challenged, details)

    @dbus.service.method('org.freedesktop.PolicyKit1.Authority',
                         in_signature='', out_signature='')
    def Quit(self):
        GLib.idle_add(self.loop.quit)

    def on_disconnected(self):
        print('disconnected from D-BUS, terminating')
        self.Quit()

# ----------------------------------------------------------------------------

class PolkitTestCase(unittest.TestCase):
    '''Convenient test cases involving polkit.

    Call start_polkitd() with the list of allowed actions in your test cases.
    The daemon will be automatically terminated when the test case exits.
    '''

    def __init__(self, methodName='runTest'):
        unittest.TestCase.__init__(self, methodName)
        self.polkit_pid = None

    def start_polkitd(self, allowed_actions, on_bus=None):
        '''Start test polkitd.

        This should be called in your test cases before the exercised code
        makes any polkit query. The daemon will be stopped automatically when
        the test case ends (regardless of whether its successful or failed). If
        you want to test multiple different action sets in one test case, you
        have to call stop_polkitd() before starting a new one.

        @allowed_actions is a list of PolicyKit action IDs which will be
        allowed (active/inactive sessions or user IDs will not be considered);
        all actions not in that list will be denied. If 'all' is an element of
        @allowed_actions, all actions will be allowed.

        When @on_bus string is given, the daemon will run on that D-BUS
        address, otherwise on the system D-BUS.
        '''
        assert self.polkit_pid is None, \
            'can only launch one polkitd at a time; write a separate test case or call stop_polkitd()'
        self.polkit_pid = spawn(allowed_actions, on_bus)
        self.addCleanup(self.stop_polkitd)

    def stop_polkitd(self):
        '''Stop test polkitd.

        This happens automatically when a test case ends, but is required when
        you want to test multiple different action sets in one test case.
        '''
        assert self.polkit_pid is not None, 'polkitd is not running'
        os.kill(self.polkit_pid, signal.SIGTERM)
        os.waitpid(self.polkit_pid, 0)
        self.polkit_pid = None

# ----------------------------------------------------------------------------

def _run(allowed_actions, bus_address):
    # Set up the DBus main loop
    import dbus.mainloop.glib
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

    polkitd = TestPolicyKitDaemon(allowed_actions, bus_address)
    polkitd.run()

def spawn(allowed_actions, on_bus=None):
    '''Run a TestPolicyKitDaemon instance in a separate process.

    @allowed_actions is a list of PolicyKit action IDs which will be
    allowed (active/inactive sessions or user IDs will not be considered);
    all actions not in that list will be denied. If 'all' is an element of
    @allowed_actions, all actions will be allowed.

    When @on_bus string is given, the daemon will run on that D-BUS address,
    otherwise on the system D-BUS.

    The daemon will terminate automatically when the @on_bus D-BUS goes down.
    If that does not happen (e. g. you test on the actual system/session bus),
    you need to kill it manually.

    Returns the process ID of the spawned daemon.
    '''
    pid = os.fork()
    if pid == 0:
        # child
        _run(allowed_actions, on_bus)
        os._exit(0)

    # wait until the daemon is up on the bus
    if on_bus:
        bus = dbus.bus.BusConnection(on_bus)
    elif 'DBUS_SYSTEM_BUS_ADDRESS' in os.environ:
        # dbus.SystemBus() does not recognize this env var, so we have to
        # handle that manually
        bus = dbus.bus.BusConnection(os.environ['DBUS_SYSTEM_BUS_ADDRESS'])
    else:
        bus = dbus.SystemBus()
    for timeout in range(50):
        try:
            p = dbus.Interface(bus.get_object('org.freedesktop.DBus', '/org/freedesktop/DBus'),
                               'org.freedesktop.DBus').GetConnectionUnixProcessID(
                                   bus.get_name_owner('org.freedesktop.PolicyKit1'))
            if p == pid:
                break
        except dbus.exceptions.DBusException:
            pass
        time.sleep(0.1)
    else:
        raise SystemError('test polkitd failed to start up')

    return pid

def main():
    parser = argparse.ArgumentParser(description='Simple mock polkit daemon for test suites')
    parser.add_argument('-a', '--allowed-actions', metavar='ACTION[,ACTION,...]',
                      default='', help='Comma separated list of allowed action ids')
    parser.add_argument('-b', '--bus-address',
                      help='D-BUS address to listen on (if not given, listen on system D-BUS)')
    args = parser.parse_args()

    _run(args.allowed_actions.split(','), args.bus_address)

if __name__ == '__main__':
    main()
