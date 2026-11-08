/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* Path conversion between OS/2 drive paths and host paths over the drive table. */
#ifndef TWOINE_EMULATOR_PATHS_H
#define TWOINE_EMULATOR_PATHS_H
#include "formats/executable/types.h"

/* A malloc'd OS/2 path for a host path, or NULL. */
char *lxHostPathToGuest(const char *name);
/* A malloc'd host path for an OS/2 path, matching existing names case-insensitively.
   On failure returns NULL and sets *err to an OS/2 error code. */
char *lxMakeUnixPath(const char *os2path, uint32 *err);
/* Rewrite buf in place to the existing host spelling of each element. Returns 0 when
   the path exists or is empty, -1 when only the last element is missing, -2 otherwise. */
int lxLocatePathCaseInsensitive(char *buf);
#endif
