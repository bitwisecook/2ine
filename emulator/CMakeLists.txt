# Public domain. No rights reserved. Wholly AI-authored.
# The emulator: execution dispatch, the OS/2 process runtime (lib2ine) and the loader.
add_subdirectory(abi)
add_subdirectory(execution/core)
if(NOT TWOINE_RUNTIME_ONLY)
    add_subdirectory(process)
    add_subdirectory(loader)
endif()
