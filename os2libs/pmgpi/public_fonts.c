/* Public domain. No rights reserved. Wholly AI-authored. */
/* Public font files extend the host catalogue for this process. Loads of one
   file are counted, so each must be matched by an unload. */
#include "os2libs/os2native.h"
#include "os2libs/pmgpi.h"
#include "host/font.h"
#include <stdlib.h>

static BOOL with_host_path(PSZ name, int (*action)(const char *))
{
    uint32 error = 0;
    if (!name || !*name || !GProcessState.makeUnixPath)
        return FALSE;
    char *path = GProcessState.makeUnixPath(name, &error);
    BOOL result = path && !error && action(path);
    free(path);
    return result;
}

BOOL GpiLoadPublicFonts(HAB hab, PSZ name)
{
    TRACE_NATIVE("GpiLoadPublicFonts(%u, '%s')", (uint)hab, name);
    return with_host_path(name, lx_host_font_add_file);
}

BOOL GpiUnloadPublicFonts(HAB hab, PSZ name)
{
    TRACE_NATIVE("GpiUnloadPublicFonts(%u, '%s')", (uint)hab, name);
    return with_host_path(name, lx_host_font_remove_file);
}
