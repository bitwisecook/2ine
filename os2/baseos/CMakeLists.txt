# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Base OS (IBM w45baseos): DOSCALLS and the DLLs IBM forwards to DOSCALL1.
twoine_os2_dll(
    doscalls
    doscalls/module.c
    doscalls/legacy.c
    doscalls/filesys/file.c
    doscalls/filesys/fhandle.c
    doscalls/filesys/lock.c
    doscalls/filesys/devioctl.c
    doscalls/filesys/pipe.c
    doscalls/filesys/nampipe.c
    doscalls/filesys/getset2.c
    doscalls/filesys/shutdown.c
    doscalls/filesys/finfo.c
    doscalls/filesys/extattr.c
    doscalls/filesys/dossrch.c
    doscalls/paths.c
    doscalls/guarded.c
    doscalls/loader/ldrrsrc.c
    doscalls/task/except.c
    doscalls/memory.c
    doscalls/doscall1/suballoc.c
    doscalls/threads.c
    doscalls/process.c
    doscalls/semaphores.c
    doscalls/modules.c
    doscalls/system.c
    doscalls/dev/beep.c
    doscalls/sem/semmux.c
    doscalls/doscall1/thunk.c
    doscalls/task/timapi.c
    doscalls/doscall1/queue.c
    doscalls/doscall1/session.c
    doscalls/doscall1/nls.c
    doscalls/doscall1/msg.c
    doscalls/doscall1/vio.c
    doscalls/doscall1/kbd.c
)
twoine_os2_dll(msg msg/module.c)
twoine_os2_dll(nls nls/module.c)
twoine_os2_dll(quecalls quecalls/module.c)
twoine_os2_dll(viocalls viocalls/module.c)
twoine_os2_dll(kbdcalls kbdcalls/module.c)
twoine_os2_dll(sesmgr sesmgr/module.c)
if(TWOINE_NATIVE_LOADER)
    twoine_add_native_abi(doscalls doscalls)
    twoine_add_native_abi(doscalls msg)
endif()
twoine_add_host_terminal(doscalls)
