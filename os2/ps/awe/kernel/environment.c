/* Public domain. No rights reserved. Wholly AI-authored. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "emulator/abi.h"
#include "host/sync.h"
#include <stdio.h>

enum
{
    /* Win32's limit on a variable's name or value, in characters. */
    ENVIRONMENT_STRING_LIMIT = 32767,
    /* Largest environment block SetEnvironmentVariable builds. */
    ENVIRONMENT_BLOCK_LIMIT = 65536
};

static LxHostOnce env_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *env_monitor;

static void env_initialize(void)
{
    env_monitor = lx_host_monitor_create();
    if (!env_monitor)
        abort();
}

ULONG GetEnvironmentStrings(void)
{
    ULONG address = 0;
    if (!lx_execution_address(GProcessState.execution, GProcessState.pib.environment, 1,
                              LX_GUEST_READ, &address))
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    return address;
}

ULONG GetEnvironmentVariable(PSZ name, PSZ output, ULONG capacity)
{
    if (!name || !*name || strchr(name, '=') || !capacity || !output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    lx_host_once(&env_once, env_initialize);
    lx_host_monitor_lock(env_monitor);
    size_t length = strlen(name);
    const char *value = NULL;
    for (const char *entry = GProcessState.pib.environment; entry && *entry;
         entry += strlen(entry) + 1)
        if (!strncasecmp(entry, name, length) && entry[length] == '=')
        {
            value = entry + length + 1;
            break;
        }
    ULONG result = 0;
    if (value)
    {
        size_t n = strlen(value);
        result = (ULONG)(n < capacity ? n : n + 1);
        if (n < capacity)
            memcpy(output, value, n + 1);
    }
    else
        open32SetError(ERROR_ENVVAR_NOT_FOUND);
    lx_host_monitor_unlock(env_monitor);
    return result;
}

BOOL SetEnvironmentVariable(PSZ name, PSZ value)
{
    if (!name || !*name || strchr(name, '=') || strlen(name) > ENVIRONMENT_STRING_LIMIT ||
        (value && strlen(value) > ENVIRONMENT_STRING_LIMIT))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    lx_host_once(&env_once, env_initialize);
    lx_host_monitor_lock(env_monitor);
    const char *old = GProcessState.pib.environment;
    size_t name_size = strlen(name), size = 1;
    for (const char *p = old; p && *p; p += strlen(p) + 1)
        if (strncasecmp(p, name, name_size) || p[name_size] != '=')
            size += strlen(p) + 1;
    /* "=" and the terminator. */
    if (value)
        size += name_size + strlen(value) + 2;
    /* An empty block is still two NULs. */
    if (size == 1)
        size = 2;
    LxGuestMapping *mapping =
        size <= ENVIRONMENT_BLOCK_LIMIT
            ? lx_execution_map(GProcessState.execution, 0,
                               (size + OPEN32_PAGE_SIZE - 1) & ~(size_t)(OPEN32_PAGE_SIZE - 1),
                               LX_GUEST_READ | LX_GUEST_WRITE, 0)
            : NULL;
    if (!mapping)
    {
        lx_host_monitor_unlock(env_monitor);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    char *begin = lx_execution_mapping_host(mapping), *out = begin;
    for (const char *p = old; p && *p; p += strlen(p) + 1)
        if (strncasecmp(p, name, name_size) || p[name_size] != '=')
        {
            size_t n = strlen(p) + 1;
            memcpy(out, p, n);
            out += n;
        }
    if (value)
    {
        memcpy(out, name, name_size);
        out += name_size;
        *out++ = '=';
        size_t n = strlen(value) + 1;
        memcpy(out, value, n);
        out += n;
    }
    *out = 0;
    if (out == begin)
        out[1] = 0;
    GProcessState.pib.environment = begin;
    if (GProcessState.guest_pib)
    {
        LxGuestPib *pib = lx_execution_mapping_host(GProcessState.guest_pib);
        lx_guest_store_u32(&pib->environment, lx_execution_mapping_address(mapping));
    }
    /* Old blocks remain readable to guest code holding an earlier environment
       pointer. All mappings are reclaimed by execution teardown. */
    lx_host_monitor_unlock(env_monitor);
    return TRUE;
}
