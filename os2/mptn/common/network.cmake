# Public domain. No rights reserved. Wholly AI-authored.
# Socket structure layouts and conversions, socket error recording and host lookups,
# shared by TCPIP32, SO32DLL and TCP32DLL.
# twoine_os2_network uses the OS/2 application ABI of the DLLs;
# twoine_os2_network_host is the host-ABI build for the host network tests.
add_library(
    twoine_os2_network
    OBJECT
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/network.c"
    "${CMAKE_CURRENT_LIST_DIR}/socket_errors.c"
    "${CMAKE_CURRENT_LIST_DIR}/resolver.c"
)
twoine_application_target(twoine_os2_network)
set_target_properties(twoine_os2_network PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(twoine_os2_network_host OBJECT EXCLUDE_FROM_ALL "${CMAKE_CURRENT_LIST_DIR}/network.c")
target_include_directories(
    twoine_os2_network_host
    PUBLIC "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/host/include"
)
