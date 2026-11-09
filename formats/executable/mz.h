/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* The DOS stub header that precedes NE and LX headers; names and values follow the Toolkit's
   newexe.h. */

#ifndef TWOINE_FORMAT_MZ_H
#define TWOINE_FORMAT_MZ_H

enum
{
    ENEWHDR = 0x003C, /* offset of the 32-bit file offset of the NE or LX header */
    ENEWEXE = 0x0040  /* size of the DOS stub header */
};

#endif
