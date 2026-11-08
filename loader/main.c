/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "loader/internal.h"
#include "hal/host_desktop_bundle.h"
#include "hal/host_fatal.h"
#include "hal/host_operation.h"
#include "loader/network_config.h"

int main(int argc, char **argv, char **envp)
{
    if (argc > 1 && !strcmp(argv[1], LX_FATAL_DIALOG_ARGUMENT))
        lx_host_process_exit((uint32_t)lx_host_fatal_dialog(argc - 2, argv + 2));
#if !defined(_WIN32)
    if (!lx_host_operation_join())
    {
        fprintf(stderr, "Invalid installer operation channels\n");
        return 1;
    }
#endif
    if (argc < 2 || (argc == 2 && !strcmp(argv[1], "--operation")))
    {
        fprintf(stderr, "USAGE: %s [--operation] <program.exe> [...programargs...]\n",
                argc && argv && argv[0] ? argv[0] : "lx_loader");
        return 1;
    }
    if (!strcmp(argv[1], "--operation"))
    {
#if defined(_WIN32)
        fprintf(stderr, "Installer operation supervision is unavailable on this host\n");
        return 1;
#else
        return lx_host_operation_supervise(argv[0], argv + 2, envp);
#endif
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
        lx_host_fatal(LX_FATAL_LOAD_FAILURE, error, NULL);
    lx_host_fatal_add_context(lxWriteFatalModules);

    /* Keep the command and environment in one 64-KiB guest segment. */
    const size_t command_offset =
        (size_t)(GLoaderState.pib.pib_pchcmd - GLoaderState.pib.pib_pchenv);
    LxGuestMapping *parameters =
        lx_execution_map(GLoaderState.execution, 0x07000000, 0x10000, 3, 1);
    if (!parameters)
        lx_host_fatal(LX_FATAL_LOAD_FAILURE, "Cannot allocate guest process parameters", NULL);
    char *environment = lx_execution_mapping_host(parameters);
    memcpy(environment, GLoaderState.pib.pib_pchenv, 0x10000);
    free(GLoaderState.pib.pib_pchenv);
    GLoaderState.pib.pib_pchenv = environment;
    GLoaderState.pib.pib_pchcmd = environment + command_offset;
    GLoaderState.guest_pib = lx_execution_map(GLoaderState.execution, 0x07010000, 0x1000, 3, 1);
    if (!GLoaderState.guest_pib)
        lx_host_fatal(LX_FATAL_LOAD_FAILURE, "Cannot allocate guest PIB", NULL);
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
    GLoaderState.findModuleOrdinal = findModuleOrdinal;
    GLoaderState.allocSegment = lxAllocSegment;
    GLoaderState.freeSegment = lxFreeSegment;
    GLoaderState.createCodeAlias = lxCreateCodeAlias;
    GLoaderState.findSelector = lxFindSelector;
    GLoaderState.freeSelector = lxFreeSelector;
    GLoaderState.convert1616to32 = lxConvert1616to32;
    GLoaderState.convert32to1616 = lxConvert32to1616;
    GLoaderState.loadModule = loadModuleByPathOrModuleName;
    GLoaderState.loadModuleWithError = loadModuleWithError;
    GLoaderState.freeModule = freeLxModule;
    GLoaderState.makeUnixPath = lxMakeUnixPath;
    GLoaderState.terminate = lxTerminate;
    lxPrepareNetworkConfiguration();

    if (getenv("TWOINE_TRACE_ARGS"))
    {
        fprintf(stderr, "2INE ARGS:");
        for (int i = 0; i < argc; i++)
            fprintf(stderr, " [%d]='%s'", i, argv[i]);
        fprintf(stderr, "\n2INE CMD='%s'\n",
                GLoaderState.pib.pib_pchcmd ? GLoaderState.pib.pib_pchcmd : "(null)");
    }

    if (!lxRegisterProcessPeer())
        lx_host_fatal(LX_FATAL_LOAD_FAILURE,
                      "Cannot register this OS/2 process in its ball IPC scope", NULL);

    if (!strcmp(argv[1], "--rexx-service"))
        return runRexxService();

    const char *modulename = GLoaderState.subprocess ? getenv("IS_2INE") : argv[1];
    uint32 path_error = 0;
    char *mapped = NULL;
    if (!GLoaderState.subprocess && strlen(modulename) >= 2 && modulename[1] == ':')
    {
        mapped = lxMakeUnixPath(modulename, &path_error);
        if (!mapped)
        {
            char summary[512];
            snprintf(summary, sizeof summary, "Cannot resolve guest program '%s' (OS/2 error %u)",
                     modulename, path_error);
            lx_host_fatal(LX_FATAL_LOAD_FAILURE, summary, NULL);
        }
        modulename = mapped;
    }
    /* The loader presents the program itself, not the loader, on the host desktop. */
    lx_desktop_bundle_exec(modulename, argv);

    LxModule *lxmod = loadModuleByPath(modulename);
    if (lxmod != NULL)
    {
        free(mapped);
        runModule(lxmod);
        return 1;
    }
    char summary[768];
    lxDescribeModuleFailure(modulename, summary, sizeof summary);
    lx_host_fatal(LX_FATAL_LOAD_FAILURE, summary, NULL);
}
