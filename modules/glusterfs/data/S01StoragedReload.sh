#!/bin/sh

dbus-send --system                               \
--dest=org.storaged.Storaged --type=method_call  \
/org/storaged/Storaged/Manager                   \
org.storaged.Storaged.Manager.GlusterFS.Reload
