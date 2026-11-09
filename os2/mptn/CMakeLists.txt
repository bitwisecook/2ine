# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# TCP/IP (MPTN): the 32-bit sockets DLL and the two 16-bit-stack API sets.
twoine_os2_dll(tcpip32 tcpip32/module.c)
twoine_os2_dll(so32dll so32dll/module.c)
target_link_libraries(tcpip32 PRIVATE twoine_os2_network)
target_link_libraries(so32dll PRIVATE twoine_os2_network)
twoine_os2_dll(tcp32dll tcp32dll/module.c)
target_link_libraries(tcp32dll PRIVATE twoine_os2_network)
