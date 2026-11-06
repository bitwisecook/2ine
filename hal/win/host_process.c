/* Public domain. No rights reserved. Wholly AI-authored. */
/* Process bootstrap queries. Returned strings/buffers belong to the caller. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "hal/host.h"
#include "hal/host_process.h"
#include "../common/process.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
static char *utf8(const wchar_t *source)
{
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1, NULL, 0, NULL, NULL);
    if (!size)
        return NULL;
    char *text = malloc((size_t)size);
    if (text &&
        !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1, text, size, NULL, NULL))
    {
        free(text);
        text = NULL;
    }
    return text;
}
char *lx_host_command_line(size_t *size)
{
    if (!size)
    {
        errno = EINVAL;
        return NULL;
    }
    *size = 0;
    int argc;
    char **argv;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide)
        return NULL;
    argv = calloc((size_t)argc + 1, sizeof *argv);
    if (!argv)
    {
        LocalFree(wide);
        return NULL;
    }
    for (int i = 0; i < argc; i++)
    {
        argv[i] = utf8(wide[i]);
        if (!argv[i])
        {
            for (int j = 0; j < i; j++)
                free(argv[j]);
            free(argv);
            LocalFree(wide);
            return NULL;
        }
    }
    LocalFree(wide);
    char *buffer = lx_host_pack_arguments(argc, argv, size);
    for (int i = 0; i < argc; i++)
        free(argv[i]);
    free(argv);
    return buffer;
}
char *lx_host_executable_path(void)
{
    for (DWORD size = 256; size <= 32768; size *= 2)
    {
        wchar_t *buffer = malloc(size * sizeof *buffer);
        if (!buffer)
            return NULL;
        DWORD n = GetModuleFileNameW(NULL, buffer, size);
        if (n && n < size)
        {
            char *result = utf8(buffer);
            free(buffer);
            return result;
        }
        free(buffer);
    }
    return NULL;
}

void lx_host_process_ids(uint32_t *process, uint32_t *parent)
{
    *process = GetCurrentProcessId();
    *parent = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry = {0};
        entry.dwSize = sizeof entry;
        if (Process32FirstW(snapshot, &entry))
            do
            {
                if (entry.th32ProcessID == *process)
                {
                    *parent = entry.th32ParentProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        CloseHandle(snapshot);
    }
}
size_t lx_host_stack_limit(void)
{
    return 8 * 1024 * 1024;
}
void lx_host_process_exit(uint32_t status)
{
    ExitProcess(status);
}

char *const *lx_host_environment(void)
{
    extern char **_environ;
    return _environ;
}

int lx_host_process_set_priority(LxHostProcessPriority priority)
{
    DWORD selected;
    switch (priority)
    {
        case LX_HOST_PROCESS_PRIORITY_NORMAL:
            selected = NORMAL_PRIORITY_CLASS;
            break;
        case LX_HOST_PROCESS_PRIORITY_IDLE:
            selected = IDLE_PRIORITY_CLASS;
            break;
        case LX_HOST_PROCESS_PRIORITY_REALTIME:
            errno = ENOTSUP;
            return -1;
        default:
            errno = EINVAL;
            return -1;
    }
    if (SetPriorityClass(GetCurrentProcess(), selected))
        return 0;
    DWORD error = GetLastError();
    errno = error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD ? EACCES : EIO;
    return -1;
}
