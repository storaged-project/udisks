## How to setup a develop environment.

## Files
* mp_module_iface.c

    Implemented the public module public method defined by
    modules/storagedmoduleifacetypes.h
    Cache multipath data for 30 seconds on all 'add' udev action.

* mp_linux_drive.c

    Handle "org.storaged.Storaged.Drive.Multipath" and
    "org.storaged.Storaged.Multipath".

* mp_linux_block.c

    As mp_linux_drive.c already created
    org.storaged.Storaged.Multipath.PathGroup.Path object, this file
    only update/remove it when got udev event.
    When certain disk been marked as offline via sysfs, only dm-X device
    will got udev event.

* mp_linux_manager.c

    Empty interface for org.storaged.Storaged.Manager.LSM.

* mp_types.h

    Header shared by above files.

## Workflow
### General
 * Hook to org.storaged.Storaged.Drive and org.storaged.Storaged.Block udev
   event.
 * Use storaged_linux_device_multipath_name() to find out whether a multipath
   device.
 * If so, call std_lx_blk_mp_update() and std_lx_drv_mp_update().

### std_lx_drv_mp_update()

### std_lx_blk_mp_update()


## TODO
 * Update org.storaged.Storaged.Multipath on 'change' or other uevent.
