/* Public domain. No rights reserved. Wholly AI-authored. */
/* Process bootstrap queries. Returned strings/buffers belong to the caller. */
#define _GNU_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "host.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif
#if !defined(_WIN32)
#include <unistd.h>
#include <sys/resource.h>
#endif
#if defined(_WIN32)
static char *utf8(const wchar_t *source)
{
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1, NULL, 0, NULL, NULL);
    if (!size) return NULL;
    char *text = malloc((size_t) size);
    if (text && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1, text, size, NULL, NULL)) {
        free(text); text = NULL;
    }
    return text;
}
#endif
char *lx_host_command_line(size_t *size)
{
    if (!size) { errno = EINVAL; return NULL; }
    *size = 0;
#if defined(__APPLE__) || defined(_WIN32)
    int argc;
    char **argv;
#if defined(__APPLE__)
    argc = *_NSGetArgc(); argv = *_NSGetArgv();
#else
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide) return NULL;
    argv = calloc((size_t) argc + 1, sizeof *argv);
    if (!argv) { LocalFree(wide); return NULL; }
    for (int i = 0; i < argc; i++) {
        argv[i] = utf8(wide[i]);
        if (!argv[i]) { for (int j = 0; j < i; j++) free(argv[j]); free(argv); LocalFree(wide); return NULL; }
    }
    LocalFree(wide);
#endif
    size_t total = 0;
    for (int i = 0; i < argc; i++) total += strlen(argv[i]) + 1;
    char *buffer = malloc(total ? total : 1);
    if (buffer) {
        char *p = buffer;
        for (int i = 0; i < argc; i++) { size_t n = strlen(argv[i]) + 1; memcpy(p, argv[i], n); p += n; }
        *size = total;
    }
#if defined(_WIN32)
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
#endif
    return buffer;
#else
    FILE *input = fopen("/proc/self/cmdline", "rb");
    if (!input) return NULL;
    char *buffer = NULL;
    size_t used = 0;
    for (;;) {
        char *next = realloc(buffer, used + 4096);
        if (!next) { free(buffer); fclose(input); return NULL; }
        buffer = next;
        size_t n = fread(buffer + used, 1, 4096, input); used += n;
        if (n < 4096) {
            if (ferror(input)) { free(buffer); fclose(input); return NULL; }
            break;
        }
    }
    fclose(input); *size = used; return buffer;
#endif
}
char *lx_host_executable_path(void)
{
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    char *buffer = malloc(size);
    if (!buffer) return NULL;
    if (_NSGetExecutablePath(buffer, &size) != 0) { free(buffer); return NULL; }
    char *resolved = realpath(buffer, NULL); free(buffer); return resolved;
#elif defined(_WIN32)
    for (DWORD size = 256; size <= 32768; size *= 2) {
        wchar_t *buffer = malloc(size * sizeof *buffer);
        if (!buffer) return NULL;
        DWORD n = GetModuleFileNameW(NULL, buffer, size);
        if (n && n < size) { char *result = utf8(buffer); free(buffer); return result; }
        free(buffer);
    }
    return NULL;
#else
    for (size_t size = 256; size <= 1024 * 1024; size *= 2) {
        char *buffer = malloc(size);
        if (!buffer) return NULL;
        ssize_t n = readlink("/proc/self/exe", buffer, size - 1);
        if (n >= 0 && (size_t) n < size - 1) { buffer[n] = 0; return buffer; }
        free(buffer); if (n < 0) return NULL;
    }
    return NULL;
#endif
}

void lx_host_process_ids(uint32_t *process, uint32_t *parent)
{
#if defined(_WIN32)
    *process = GetCurrentProcessId(); *parent = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry = {0}; entry.dwSize = sizeof entry;
        if (Process32FirstW(snapshot, &entry)) do {
            if (entry.th32ProcessID == *process) { *parent = entry.th32ParentProcessID; break; }
        } while (Process32NextW(snapshot, &entry));
        CloseHandle(snapshot);
    }
#else
    *process = (uint32_t) getpid(); *parent = (uint32_t) getppid();
#endif
}
size_t lx_host_stack_limit(void)
{
#if !defined(_WIN32)
    struct rlimit limit;
    if (!getrlimit(RLIMIT_STACK, &limit) && limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur <= SIZE_MAX)
        return (size_t) limit.rlim_cur;
#endif
    return 8 * 1024 * 1024;
}
void lx_host_process_exit(uint32_t status)
{
#if defined(_WIN32)
    ExitProcess(status);
#else
    _exit((int) (status & 255));
#endif
}
