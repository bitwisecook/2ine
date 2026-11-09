/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* Relocation record values shared by the NE and LX formats; names and values follow the
   Toolkit's newexe.h, which exe386.h extends. */

#ifndef TWOINE_FORMAT_RELOCATIONS_H
#define TWOINE_FORMAT_RELOCATIONS_H

enum
{
    /* Source byte (nr_stype) */
    NRSTYP = 0x0f,
    NRSBYT = 0x00,
    NRSSEG = 0x02,
    NRSPTR = 0x03,
    NRSOFF = 0x05,
    NRPTR48 = 0x06,
    NROFF32 = 0x07,
    NRSOFF32 = 0x08,

    /* Flags byte (nr_flags) */
    NRRTYP = 0x03,
    NRRINT = 0x00,
    NRRORD = 0x01,
    NRRNAM = 0x02,
    NRADD = 0x04,
    NRICHAIN = 0x08
};

#endif
