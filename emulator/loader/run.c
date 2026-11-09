/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "emulator/loader.h"
#include "emulator/loader/internal.h"
#include "host/desktop.h"
#include "host/fatal.h"
#include "emulator/resolver_files.h"

enum
{
    GUEST_PARAMETERS_ADDRESS = 0x07000000,
    GUEST_PIB_ADDRESS = 0x07010000,
    GUEST_PIB_MAPPING_SIZE = 0x1000 /* one page */
};

int lx_loader_run(int argc, char **argv)
{
    GProcessState.running = 0;
    GProcessState.deinitOs2Tib(GProcessState.main_tib_selector);
    GProcessState.main_tib_selector = 0;

    const LxExecutionServices services = {NULL,
                                          executableAt,
                                          describeExecutionAddress,
                                          executionReturned,
                                          LXTIBSIZE,
                                          sizeof(LxGuestTib),
                                          sizeof(LxGuestTib) + sizeof(LxGuestTib2)};
    char error[256];
    GProcessState.execution =
        lx_execution_create(getenv("TWOINE_BACKEND"), &services, error, sizeof error);
    if (!GProcessState.execution)
        lx_host_fatal(LX_FATAL_LOAD_FAILURE, error, NULL);
    lx_host_fatal_add_context(lxWriteFatalModules);

    /* Fixed guest addresses keep the parameter segment and PIB out of the module arena. */
    const size_t command_offset =
        (size_t)(GProcessState.pib.command - GProcessState.pib.environment);
    LxGuestMapping *parameters =
        lx_execution_map(GProcessState.execution, GUEST_PARAMETERS_ADDRESS,
                         LX_ENVIRONMENT_SEGMENT_SIZE, LX_GUEST_READ | LX_GUEST_WRITE, 1);
    if (!parameters)
        lx_host_fatal(LX_FATAL_LOAD_FAILURE, "Cannot allocate guest process parameters", NULL);
    char *environment = lx_execution_mapping_host(parameters);
    memcpy(environment, GProcessState.pib.environment, LX_ENVIRONMENT_SEGMENT_SIZE);
    free(GProcessState.pib.environment);
    GProcessState.pib.environment = environment;
    GProcessState.pib.command = environment + command_offset;
    GProcessState.guest_pib =
        lx_execution_map(GProcessState.execution, GUEST_PIB_ADDRESS, GUEST_PIB_MAPPING_SIZE,
                         LX_GUEST_READ | LX_GUEST_WRITE, 1);
    if (!GProcessState.guest_pib)
        lx_host_fatal(LX_FATAL_LOAD_FAILURE, "Cannot allocate guest PIB", NULL);
    LxGuestPib *pib = lx_execution_mapping_host(GProcessState.guest_pib);
    lx_guest_store_u32(&pib->process_id, GProcessState.pib.process_id);
    lx_guest_store_u32(&pib->parent_id, GProcessState.pib.parent_id);
    lx_guest_store_u32(&pib->command,
                       lx_execution_mapping_address(parameters) + (uint32)command_offset);
    lx_guest_store_u32(&pib->environment, lx_execution_mapping_address(parameters));
    lx_guest_store_u32(&pib->status, GProcessState.pib.status);
    lx_guest_store_u32(&pib->type, GProcessState.pib.type);

    /* From here TIBs, 16-bit selectors and OS/2 paths go through the execution
       instance and the drive table. */
    GProcessState.execution_bound = 1;
    GProcessState.findModuleOrdinal = findModuleOrdinal;
    GProcessState.loadModule = loadModuleByPathOrModuleName;
    GProcessState.loadModuleWithError = loadModuleWithError;
    GProcessState.freeModule = freeLxModule;
    GProcessState.terminate = lxTerminate;
    lxPrepareNetworkConfiguration();

    if (getenv("TWOINE_TRACE_ARGS"))
    {
        fprintf(stderr, "2INE ARGS:");
        for (int i = 0; i < argc; i++)
            fprintf(stderr, " [%d]='%s'", i, argv[i]);
        fprintf(stderr, "\n2INE CMD='%s'\n",
                GProcessState.pib.command ? GProcessState.pib.command : "(null)");
    }

    if (!lxRegisterProcessPeer())
        lx_host_fatal(LX_FATAL_LOAD_FAILURE,
                      "Cannot register this OS/2 process in its ball IPC scope", NULL);

    if (!strcmp(argv[1], "--rexx-service"))
        return runRexxService();

    const char *modulename = GProcessState.subprocess ? getenv("IS_2INE") : argv[1];
    uint32 path_error = 0;
    char *mapped = NULL;
    if (!GProcessState.subprocess && strlen(modulename) >= 2 && modulename[1] == ':')
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
