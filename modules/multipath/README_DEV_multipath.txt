## How to setup a develop environment.

## Files
* mp_module_iface.c

    Implemented the public module public method defined by
    modules/udisksmoduleifacetypes.h
    Cache multipath data for 30 seconds on all 'add' udev action.
    Cache refresh is trigger by 'change' action or given search not found.
    Up on cache refresh, "org.freedesktop.UDisks2.Multipath" objects will be
    created or updated.

* mp_linux_drive.c

    Update "org.freedesktop.UDisks2.Drive.Multipath" by pointing it to
    "org.freedesktop.UDisks2.Multipath" object.

* mp_linux_block.c

    Update "org.freedesktop.UDisks2.Drive.Multipath" by pointing it to
    "org.freedesktop.UDisks2.Multipath" object.

* mp_linux_manager.c

    Empty interface for org.freedesktop.UDisks2.Manager.LSM.

* mp_types.h

    Header shared by above files.

## Workflow
 * Hook to org.freedesktop.UDisks2.Drive and org.freedesktop.UDisks2.Block udev
   event.
 * Use udisks_linux_device_multipath_name() to find out whether a multipath
   device.
 * If so, call ud_lx_blk_mp_update() and ud_lx_drv_mp_update().
