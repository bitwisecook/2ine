/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* The OS/2 program loader, entered by the lx_loader program. */
#ifndef TWOINE_EMULATOR_LOADER_H
#define TWOINE_EMULATOR_LOADER_H

/* Load the OS/2 program named by argv[1] (or the subprocess image named by
   IS_2INE) into this process and run it, or run the REXX service for
   "--rexx-service". argv[0] is the loader. Returns only on failure; fatal
   load errors end the process through the host fatal report. */
int lx_loader_run(int argc, char **argv);

#endif
