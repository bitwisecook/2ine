# Public domain. No rights reserved. Wholly AI-authored.
# Linux host adapter, by function. Shared POSIX code is in host/posix/.
twoine_host_function(system system/system.c)
twoine_host_function(process process/self.c)
twoine_host_function(thread thread/sync_hooks.c)
twoine_host_function(fs fs/ranges.c fs/identity.c fs/metadata.c)
twoine_host_function(net net/icmp.c net/interfaces.c)
twoine_host_function(fatal fatal/report_hooks.c)
twoine_host_function(print print/zenity_dialog.c)
twoine_host_function(opengl opengl/driver_policy.c)
if(TWOINE_MESA_ROOT)
    twoine_host_requires(opengl LINK_DIRECTORY "${TWOINE_MESA_ROOT}/lib")
endif()
