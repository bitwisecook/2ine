# Derived from original 2ine; zlib-style license. See ../../LICENSE.txt.
# Workplace Shell (IBM w45wpsh): PMWP and the spooler PMSPL.
twoine_os2_dll(
    pmwp
    pmwp/module.c
    pmwp/store.c
    pmwp/objects.c
    pmwp/classes.c
    pmwp/open.c
    pmwp/som_runtime.c
    pmwp/som_tables.c
    pmwp/wpobject.c
    pmwp/som_views.c
    pmwp/som_classes.c
    pmwp/som_extended.c
)
twoine_os2_dll(pmspl pmspl/module.c)
target_link_libraries(
    pmwp
    PRIVATE doscalls pmctls pmgpi pmmerge twoine_os2_config
)
target_link_libraries(pmspl PRIVATE pmmerge)
