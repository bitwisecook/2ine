# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Base OS (IBM w45baseos): DOSCALLS, and the DLLs that forward to it as in Warp 4.52.
twoine_os2_dll(
    doscalls
    doscalls/module.c
    doscalls/guarded.c
    doscalls/dev/beep.c
    doscalls/dev/tmr.c
    doscalls/doscall1/info_blocks.c
    doscalls/doscall1/kbd.c
    doscalls/doscall1/libpath.c
    doscalls/doscall1/msg.c
    doscalls/doscall1/must_complete.c
    doscalls/doscall1/nls.c
    doscalls/doscall1/queue.c
    doscalls/doscall1/searchpath.c
    doscalls/doscall1/session.c
    doscalls/doscall1/suballoc.c
    doscalls/doscall1/suballoc16.c
    doscalls/doscall1/thread_memory.c
    doscalls/doscall1/thunk.c
    doscalls/doscall1/vio.c
    doscalls/filesys/codepg.c
    doscalls/filesys/devioctl.c
    doscalls/filesys/dossrch.c
    doscalls/filesys/extattr.c
    doscalls/filesys/fhandle.c
    doscalls/filesys/file.c
    doscalls/filesys/finfo.c
    doscalls/filesys/fsctl.c
    doscalls/filesys/fsinfo.c
    doscalls/filesys/getset2.c
    doscalls/filesys/lock.c
    doscalls/filesys/metachar.c
    doscalls/filesys/nampipe.c
    doscalls/filesys/path.c
    doscalls/filesys/pipe.c
    doscalls/filesys/shutdown.c
    doscalls/loader/ldrrsrc.c
    doscalls/loader/ldrrun.c
    doscalls/loader/ldrsubr.c
    doscalls/memory/selwrk.c
    doscalls/memory/vmapi.c
    doscalls/perf/qstatec.c
    doscalls/sem/sem.c
    doscalls/sem/semevent.c
    doscalls/sem/semmtx.c
    doscalls/sem/semmux.c
    doscalls/sem/semtools.c
    doscalls/task/except.c
    doscalls/task/procstat.c
    doscalls/task/schedapi.c
    doscalls/task/task.c
    doscalls/task/timapi.c
    doscalls/task/timserv.c
    doscalls/task/tkexec.c
    doscalls/task/tkextlst.c
    doscalls/task/tkforce.c
    doscalls/task/tkinit.c
    doscalls/task/tksleep.c
    doscalls/task/tkthrd.c
    doscalls/task/tkv1api.c
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
