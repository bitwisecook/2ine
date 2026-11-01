/* Public domain. No rights reserved. Wholly AI-authored. */
/* Child lifecycle and status normalization; no OS/2 data layouts cross here. */
#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "hal/host_process.h"
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
struct LxHostChild
{
    HANDLE handle;
    DWORD id;
};
int lx_host_child_terminate(LxHostChild *child)
{
    if (WaitForSingleObject(child->handle, 0) == WAIT_OBJECT_0)
        return 1;
    return TerminateProcess(child->handle, 1) ? 0 : -1;
}
static wchar_t *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n)
        return NULL;
    wchar_t *w = malloc((size_t)n * sizeof *w);
    if (w && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n))
    {
        free(w);
        w = NULL;
    }
    return w;
}
LxHostChild *lx_host_child_spawn(const char *path, char *const *argv, char *const *env)
{
    size_t size = 1;
    for (unsigned i = 0; argv[i]; i++)
        size += 2 * strlen(argv[i]) + 4;
    char *line = malloc(size), *p = line;
    if (!line)
        return NULL;
    for (unsigned i = 0; argv[i]; i++)
    {
        if (i)
            *p++ = ' ';
        *p++ = '"';
        const char *s = argv[i];
        for (;;)
        {
            size_t slashes = 0;
            while (*s == '\\')
            {
                slashes++;
                s++;
            }
            size_t copies = (*s == '"' || !*s) ? slashes * 2 : slashes;
            while (copies--)
                *p++ = '\\';
            if (!*s)
                break;
            if (*s == '"')
                *p++ = '\\';
            *p++ = *s++;
        }
        *p++ = '"';
    }
    *p = 0;
    wchar_t *command = wide(line), *executable = wide(path);
    free(line);
    size_t env_size = 1;
    for (unsigned i = 0; env[i]; i++)
        env_size += strlen(env[i]) + 1;
    wchar_t *environment = calloc(env_size + 1, sizeof *environment), *out = environment;
    int ok = command && executable && environment;
    for (unsigned i = 0; ok && env[i]; i++)
    {
        wchar_t *item = wide(env[i]);
        if (!item)
        {
            ok = 0;
            break;
        }
        size_t n = wcslen(item) + 1;
        memcpy(out, item, n * sizeof *out);
        out += n;
        free(item);
    }
    STARTUPINFOW startup = {0};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process = {0};
    LxHostChild *child = calloc(1, sizeof *child);
    if (ok && child)
        ok = CreateProcessW(executable, command, NULL, NULL, TRUE, CREATE_UNICODE_ENVIRONMENT,
                            environment, NULL, &startup, &process) != 0;
    else
        ok = 0;
    free(command);
    free(executable);
    free(environment);
    if (!ok)
    {
        free(child);
        return NULL;
    }
    CloseHandle(process.hThread);
    child->handle = process.hProcess;
    child->id = process.dwProcessId;
    return child;
}
int lx_host_child_wait(LxHostChild *child, int nohang, LxHostChildStatus *status)
{
    DWORD rc = WaitForSingleObject(child->handle, nohang ? 0 : INFINITE), result;
    if (rc == WAIT_TIMEOUT)
        return 0;
    if (rc != WAIT_OBJECT_0 || !GetExitCodeProcess(child->handle, &result))
        return -1;
    status->reason = result >= 0xc0000000u ? LX_HOST_CHILD_EXCEPTION : LX_HOST_CHILD_EXITED;
    status->result = result;
    return 1;
}
void lx_host_child_release(LxHostChild *child)
{
    if (child)
    {
        CloseHandle(child->handle);
        free(child);
    }
}
uint32_t lx_host_child_id(const LxHostChild *child)
{
    return (uint32_t)child->id;
}
