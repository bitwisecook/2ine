/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/pmwin/internal.h"
#include "os2libs/generated/pmwin-lx.h"

LX_PRIVATE uint32 GHABCounter;
LX_PRIVATE uint32 GHMQCounter;

LX_PRIVATE Window desktop_window;
