/* Public domain. No rights reserved. Wholly AI-authored. */
/* Child lifecycle and status normalization; no OS/2 data layouts cross here. */
#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "hal/host_process.h"
#include "hal/host_operation.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct LxHostChild
{
    HANDLE handle, thread;
    DWORD id;
};

int lx_host_child_terminate(LxHostChild *child)
{
    if (WaitForSingleObject(child->handle, 0) == WAIT_OBJECT_0)
        return 1;
    return TerminateProcess(child->handle, 1) ? 0 : -1;
}

int lx_host_child_suspend(LxHostChild *child)
{
    if (WaitForSingleObject(child->handle, 0) == WAIT_OBJECT_0)
        return 1;
    return SuspendThread(child->thread) == (DWORD)-1 ? -1 : 0;
}

int lx_host_child_resume(LxHostChild *child)
{
    if (WaitForSingleObject(child->handle, 0) == WAIT_OBJECT_0)
        return 1;
    return ResumeThread(child->thread) == (DWORD)-1 ? -1 : 0;
}

int lx_host_child_set_priority(LxHostChild *child, LxHostProcessPriority priority)
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
    if (SetPriorityClass(child->handle, selected))
        return 0;
    DWORD error = GetLastError();
    errno = error == ERROR_ACCESS_DENIED || error == ERROR_PRIVILEGE_NOT_HELD ? EACCES : EIO;
    return -1;
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

static LxHostChild *spawn_child(const char *path, char *const *argv, char *const *env,
                                DWORD creation_flags)
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
        ok = CreateProcessW(executable, command, NULL, NULL, TRUE,
                            CREATE_UNICODE_ENVIRONMENT | creation_flags, environment, NULL,
                            &startup, &process) != 0;
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
    child->thread = process.hThread;
    child->handle = process.hProcess;
    child->id = process.dwProcessId;
    return child;
}

LxHostChild *lx_host_child_spawn(const char *path, char *const *argv, char *const *env)
{
    return spawn_child(path, argv, env, 0);
}

/* CreateProcessW validates the image before the suspended child exists. */
LxHostChild *lx_host_child_spawn_suspended(const char *path, char *const *argv, char *const *env)
{
    return spawn_child(path, argv, env, CREATE_SUSPENDED);
}

LxHostChild *lx_host_child_spawn_group(const char *path, char *const *argv, char *const *env)
{
    return spawn_child(path, argv, env, CREATE_NEW_PROCESS_GROUP);
}

LxHostChild *lx_host_child_spawn_service(const char *path, char *const *argv, char *const *env)
{
    size_t count = 0, output = 0;
    while (env && env[count])
        count++;
    char **clean = calloc(count + 1, sizeof *clean);
    if (!clean)
        return NULL;
    for (size_t i = 0; i < count; i++)
        if (strncmp(env[i], LX_HOST_OPERATION_LEASE "=", sizeof LX_HOST_OPERATION_LEASE) &&
            strncmp(env[i], LX_HOST_OPERATION_OWNER "=", sizeof LX_HOST_OPERATION_OWNER))
            clean[output++] = env[i];
    LxHostChild *child = spawn_child(path, argv, clean, CREATE_NEW_PROCESS_GROUP);
    free(clean);
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
    status->signal = 0;
    return 1;
}

void lx_host_child_release(LxHostChild *child)
{
    if (child)
    {
        CloseHandle(child->thread);
        CloseHandle(child->handle);
        free(child);
    }
}

uint32_t lx_host_child_id(const LxHostChild *child)
{
    return (uint32_t)child->id;
}
