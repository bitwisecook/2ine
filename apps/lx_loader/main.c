/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* lx_loader: the host command line of the OS/2 program loader. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "emulator/loader.h"
#include "host/fatal.h"
#include "host/process.h"
#include "host/process_supervisor.h"

int main(int argc, char **argv, char **envp)
{
    if (argc > 1 && !strcmp(argv[1], LX_FATAL_DIALOG_ARGUMENT))
        lx_host_process_exit((uint32_t)lx_host_fatal_dialog(argc - 2, argv + 2));
    if (!lx_host_operation_join())
    {
        fprintf(stderr, "Invalid installer operation channels\n");
        return 1;
    }
    if (argc < 2 || (argc == 2 && !strcmp(argv[1], "--operation")))
    {
        fprintf(stderr, "USAGE: %s [--operation] <program.exe> [...programargs...]\n",
                argc && argv && argv[0] ? argv[0] : "lx_loader");
        return 1;
    }
    if (!strcmp(argv[1], "--operation"))
        return lx_host_operation_supervise(argv[0], argv + 2, envp);
    return lx_loader_run(argc, argv);
}
