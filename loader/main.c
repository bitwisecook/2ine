/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "loader/internal.h"
int main(int argc, char **argv, char **envp)
{
    if (argc < 2)
    {
        fprintf(stderr, "USAGE: %s <program.exe> [...programargs...]\n", argv[0]);
        return 1;
    }

    GLoaderState.using_lx_loader = 1;
    GLoaderState.running = 0;
    GLoaderState.deinitOs2Tib(GLoaderState.main_tib_selector);
    GLoaderState.main_tib_selector = 0;

    const LxExecutionServices services = {
        NULL,      executableAt,  describeExecutionAddress,      executionReturned,
        LXTIBSIZE, sizeof(LxTIB), sizeof(LxTIB) + sizeof(LxTIB2)};
    char error[256];
    GLoaderState.execution =
        lx_execution_create(getenv("TWOINE_BACKEND"), &services, error, sizeof error);
    if (!GLoaderState.execution)
    {
        fprintf(stderr, "2ine: %s\n", error);
        return 1;
    }

    /* Keep the command and environment in one 64-KiB guest segment. */
    const size_t command_offset =
        (size_t)(GLoaderState.pib.pib_pchcmd - GLoaderState.pib.pib_pchenv);
    LxGuestMapping *parameters =
        lx_execution_map(GLoaderState.execution, 0x07000000, 0x10000, 3, 1);
    if (!parameters)
    {
        fprintf(stderr, "Cannot allocate guest process parameters\n");
        return 1;
    }
    char *environment = lx_execution_mapping_host(parameters);
    memcpy(environment, GLoaderState.pib.pib_pchenv, 0x10000);
    free(GLoaderState.pib.pib_pchenv);
    GLoaderState.pib.pib_pchenv = environment;
    GLoaderState.pib.pib_pchcmd = environment + command_offset;
    GLoaderState.guest_pib = lx_execution_map(GLoaderState.execution, 0x07010000, 0x1000, 3, 1);
    if (!GLoaderState.guest_pib)
    {
        fprintf(stderr, "Cannot allocate guest PIB\n");
        return 1;
    }
    LxGuestPib *pib = lx_execution_mapping_host(GLoaderState.guest_pib);
    lx_guest_store_u32(&pib->process_id, GLoaderState.pib.pib_ulpid);
    lx_guest_store_u32(&pib->parent_id, GLoaderState.pib.pib_ulppid);
    lx_guest_store_u32(&pib->command,
                       lx_execution_mapping_address(parameters) + (uint32)command_offset);
    lx_guest_store_u32(&pib->environment, lx_execution_mapping_address(parameters));
    lx_guest_store_u32(&pib->status, GLoaderState.pib.pib_flstatus);
    lx_guest_store_u32(&pib->type, GLoaderState.pib.pib_ultype);

    GLoaderState.setOs2Tib = lxSetOs2Tib;
    GLoaderState.getOs2Tib = lxGetOs2Tib;
    GLoaderState.deinitOs2Tib = lxDeinitOs2Tib;
    GLoaderState.allocSegment = lxAllocSegment;
    GLoaderState.freeSegment = lxFreeSegment;
    GLoaderState.createCodeAlias = lxCreateCodeAlias;
    GLoaderState.findSelector = lxFindSelector;
    GLoaderState.freeSelector = lxFreeSelector;
    GLoaderState.convert1616to32 = lxConvert1616to32;
    GLoaderState.convert32to1616 = lxConvert32to1616;
    GLoaderState.loadModule = loadModuleByPathOrModuleName;
    GLoaderState.freeModule = freeLxModule;
    GLoaderState.makeUnixPath = lxMakeUnixPath;
    GLoaderState.terminate = lxTerminate;

    if (getenv("TWOINE_TRACE_ARGS"))
    {
        fprintf(stderr, "2INE ARGS:");
        for (int i = 0; i < argc; i++)
            fprintf(stderr, " [%d]='%s'", i, argv[i]);
        fprintf(stderr, "\n2INE CMD='%s'\n",
                GLoaderState.pib.pib_pchcmd ? GLoaderState.pib.pib_pchcmd : "(null)");
    }

    const char *modulename = GLoaderState.subprocess ? getenv("IS_2INE") : argv[1];
    uint32 path_error = 0;
    char *mapped = NULL;
    if (!GLoaderState.subprocess && strlen(modulename) >= 2 && modulename[1] == ':')
    {
        mapped = lxMakeUnixPath(modulename, &path_error);
        if (!mapped)
        {
            fprintf(stderr, "Cannot resolve guest program '%s' (OS/2 error %u)\n", modulename,
                    path_error);
            return 1;
        }
        modulename = mapped;
    }
    LxModule *lxmod = loadModuleByPath(modulename);
    free(mapped);
    if (lxmod != NULL)
        runModule(lxmod);

    return 1;
}
