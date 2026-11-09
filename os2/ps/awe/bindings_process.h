/* Public domain. No rights reserved. Wholly AI-authored. */
#include "emulator/arguments.h"
#include "os2/ps/awe/kernel/processes.h"

enum
{
    PROCESS_STRING_LIMIT = 65536,
    PROCESS_STARTUP_BYTES = 68,
    PROCESS_INFORMATION_BYTES = 16
};

/* A null guest pointer stays NULL; zero means the string could not be read. */
static int process_string(uint32 address, char **output)
{
    *output = address
                  ? lx_execution_copy_string(GProcessState.execution, address, PROCESS_STRING_LIMIT)
                  : NULL;
    return !address || *output;
}

static uint32 bridge32_CreateProcess(uint8 *a)
{
    char *application = NULL, *command = NULL, *directory = NULL;
    LxGuestArgument environment = {0};
    LxGuestBorrow startup = {0}, information = {0};
    uint32 startup_address = lx_guest_load_u32(a + 32);
    uint32 result = 0;
    if (process_string(lx_guest_load_u32(a), &application) &&
        process_string(lx_guest_load_u32(a + 4), &command) &&
        process_string(lx_guest_load_u32(a + 28), &directory) &&
        lx_guest_argument_strings(GProcessState.execution, lx_guest_load_u32(a + 24), 0,
                                  PROCESS_STRING_LIMIT, 1, &environment) &&
        (!startup_address || lx_execution_borrow(GProcessState.execution, startup_address,
                                                 PROCESS_STARTUP_BYTES, LX_GUEST_READ, &startup)) &&
        lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 36),
                            PROCESS_INFORMATION_BYTES, LX_GUEST_WRITE, &information))
        result = CreateProcess(application, command, (PVOID)(uintptr_t)lx_guest_load_u32(a + 8),
                               (PVOID)(uintptr_t)lx_guest_load_u32(a + 12),
                               lx_guest_load_u32(a + 16), lx_guest_load_u32(a + 20),
                               environment.host, directory, startup.host, information.host);
    else
        open32SetError(ERROR_INVALID_PARAMETER);
    lx_execution_release(&information);
    lx_execution_release(&startup);
    lx_guest_argument_finish(&environment, 0);
    free(directory);
    free(command);
    free(application);
    return result;
}

static uint32 bridge32_DuplicateHandle(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 12), 4, LX_GUEST_WRITE,
                             &output))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG copy = 0;
    uint32 result = DuplicateHandle(lx_guest_load_u32(a), lx_guest_load_u32(a + 4),
                                    lx_guest_load_u32(a + 8), &copy, lx_guest_load_u32(a + 16),
                                    lx_guest_load_u32(a + 20), lx_guest_load_u32(a + 24));
    if (result)
        lx_guest_store_u32(output.host, copy);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_ExitProcess(uint8 *a)
{
    ExitProcess(lx_guest_load_u32(a));
    return 0;
}

static uint32 bridge32_ExitWindows(uint8 *a)
{
    return ExitWindows(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_FatalAppExit(uint8 *a)
{
    char *message = NULL;
    /* An unreadable message must not prevent the requested termination. */
    process_string(lx_guest_load_u32(a + 4), &message);
    FatalAppExit(lx_guest_load_u32(a), message);
    free(message);
    return 0;
}

static uint32 bridge32_FatalExit(uint8 *a)
{
    FatalExit(lx_guest_load_u32(a));
    return 0;
}

static uint32 bridge32_OpenProcess(uint8 *a)
{
    return OpenProcess(lx_guest_load_u32(a), lx_guest_load_u32(a + 4), lx_guest_load_u32(a + 8));
}

static uint32 bridge32_SuspendThread(uint8 *a)
{
    return SuspendThread(lx_guest_load_u32(a));
}

static uint32 bridge32_TerminateThread(uint8 *a)
{
    return TerminateThread(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}

static uint32 bridge32_WinExec(uint8 *a)
{
    char *command = NULL;
    if (!process_string(lx_guest_load_u32(a), &command))
        return 0;
    uint32 result = WinExec(command, lx_guest_load_u32(a + 4));
    free(command);
    return result;
}

static uint32 bridge32_GetExitCodeProcess(uint8 *a)
{
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, lx_guest_load_u32(a + 4), 4, LX_GUEST_WRITE,
                             &output))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG code = 0;
    uint32 result = GetExitCodeProcess(lx_guest_load_u32(a), &code);
    if (result)
        lx_guest_store_u32(output.host, code);
    lx_execution_release(&output);
    return result;
}

static uint32 bridge32_ShellExecute(uint8 *a)
{
    char *strings[4] = {NULL};
    uint32 result = 31; /* SE_ERR_NOASSOC, as for any argument the original rejects. */
    BOOL valid = TRUE;
    for (unsigned i = 0; i < 4; i++)
        valid = process_string(lx_guest_load_u32(a + 4 + i * 4), &strings[i]) && valid;
    if (valid)
        result = ShellExecute(lx_guest_load_u32(a), strings[0], strings[1], strings[2], strings[3],
                              (LONG)lx_guest_load_u32(a + 20));
    for (unsigned i = 0; i < 4; i++)
        free(strings[i]);
    return result;
}

static uint32 bridge32_WaitForInputIdle(uint8 *a)
{
    return WaitForInputIdle(lx_guest_load_u32(a), lx_guest_load_u32(a + 4));
}
