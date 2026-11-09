# Public domain. No rights reserved. Wholly AI-authored.
# The OS/2 registry (IBM w45registry\src\reg32): REGISTRY.DLL, which implements
# the Reg* entries PMWINX forwards to.
twoine_os2_dll(registry reg32/module.c reg32/regmain/registry.c)
target_link_libraries(registry PRIVATE pmmerge)
