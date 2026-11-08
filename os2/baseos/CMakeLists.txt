# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Base OS (IBM w45baseos): DOSCALLS and the DLLs IBM forwards to DOSCALL1.
twoine_os2_dll(
    doscalls
    doscalls/module.c
    doscalls/legacy.c
    doscalls/files.c
    doscalls/fileinfo.c
    doscalls/eas.c
    doscalls/directories.c
    doscalls/paths.c
    doscalls/resources.c
    doscalls/module_resources.c
    doscalls/exceptions.c
    doscalls/memory.c
    doscalls/suballocation.c
    doscalls/threads.c
    doscalls/process.c
    doscalls/semaphores.c
    doscalls/modules.c
    doscalls/system.c
    doscalls/audio.c
    doscalls/mux_wait.c
    doscalls/unithunk.c
    doscalls/timers.c
)
twoine_os2_dll(msg msg/module.c)
twoine_os2_dll(nls nls/module.c)
twoine_os2_dll(quecalls quecalls/module.c)
twoine_os2_dll(viocalls viocalls/module.c)
twoine_os2_dll(kbdcalls kbdcalls/module.c)
twoine_os2_dll(sesmgr sesmgr/module.c)
if(TWOINE_NATIVE_LOADER)
    twoine_add_native_abi(doscalls doscalls)
    twoine_add_native_abi(msg msg)
endif()
twoine_add_host_terminal(viocalls)
target_link_libraries(kbdcalls PRIVATE viocalls)
target_link_libraries(quecalls PRIVATE doscalls)
target_link_libraries(sesmgr PRIVATE doscalls quecalls pmwin)
