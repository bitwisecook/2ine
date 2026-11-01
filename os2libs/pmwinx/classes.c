/* Public domain. No rights reserved. Wholly AI-authored. */
/* Registered Open32 classes own callback metadata, extra bytes and copied names. */
#include "os2libs/pmwinx/classes.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_sync.h"
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static Open32Class *classes;
static ULONG next_atom = 0xC000;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}
static Open32Class *find_class(ULONG instance, PSZ name)
{
    for (Open32Class *c = classes; c; c = c->next)
        if ((c->instance == instance || (c->style & 0x4000)) &&
            ((uintptr_t)name <= UINT16_MAX ? c->atom == (ULONG)(uintptr_t)name
                                           : !strcasecmp(c->name, name)))
            return c;
    return NULL;
}
Open32Class *open32ClassAcquire(ULONG instance, PSZ name)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Open32Class *c = find_class(instance, name);
    if (c)
        c->references++;
    lx_host_monitor_unlock(monitor);
    return c;
}
void open32ClassRelease(Open32Class *c)
{
    lx_host_monitor_lock(monitor);
    assert(c->references);
    c->references--;
    lx_host_monitor_unlock(monitor);
}
static void destroy_class(Open32Class *c)
{
    if (c->names)
        lx_execution_unmap(GLoaderState.execution, c->names);
    free(c->class_data);
    free(c->menu);
    free(c->name);
    free(c);
}
ULONG RegisterClass(PVOID record)
{
    if (!record || !GLoaderState.execution)
    {
        open32SetError(87);
        return 0;
    }
    const uint8 *bytes = record;
    Open32Class *c = calloc(1, sizeof *c);
    if (!c)
    {
        open32SetError(8);
        return 0;
    }
    c->style = lx_guest_load_u32(bytes);
    c->procedure = lx_guest_load_u32(bytes + 4);
    c->class_extra = lx_guest_load_u32(bytes + 8);
    c->window_extra = lx_guest_load_u32(bytes + 12);
    c->instance = lx_guest_load_u32(bytes + 16);
    c->icon = lx_guest_load_u32(bytes + 20);
    c->cursor = lx_guest_load_u32(bytes + 24);
    c->brush = lx_guest_load_u32(bytes + 28);
    c->menu_id = lx_guest_load_u32(bytes + 32);
    c->name = lx_execution_copy_string(GLoaderState.execution, lx_guest_load_u32(bytes + 36), 256);
    if (c->menu_id > UINT16_MAX)
        c->menu = lx_execution_copy_string(GLoaderState.execution, c->menu_id, 256);
    if (!c->name || !*c->name || c->class_extra > 4096 || c->window_extra > 4096 ||
        (c->menu_id > UINT16_MAX && !c->menu) ||
        !lx_execution_memory(GLoaderState.execution, c->procedure, 1, LX_GUEST_EXECUTE))
    {
        destroy_class(c);
        open32SetError(87);
        return 0;
    }
    if (c->class_extra && !(c->class_data = calloc(1, c->class_extra)))
    {
        destroy_class(c);
        open32SetError(8);
        return 0;
    }
    c->names = lx_execution_map(GLoaderState.execution, 0, 4096, 3, 0);
    if (!c->names)
    {
        destroy_class(c);
        open32SetError(8);
        return 0;
    }
    char *names = lx_execution_mapping_host(c->names);
    strcpy(names, c->name);
    if (c->menu)
        strcpy(names + 256, c->menu);
    if (!lx_execution_protect(GLoaderState.execution, c->names, LX_GUEST_READ))
    {
        destroy_class(c);
        open32SetError(8);
        return 0;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    if (find_class(c->instance, c->name) || next_atom > UINT16_MAX)
    {
        lx_host_monitor_unlock(monitor);
        destroy_class(c);
        open32SetError(1410);
        return 0;
    }
    c->atom = next_atom++;
    c->next = classes;
    classes = c;
    lx_host_monitor_unlock(monitor);
    TRACE_NATIVE("RegisterClass('%s', instance=%u, proc=%X) -> %u", c->name, c->instance,
                 c->procedure, c->atom);
    return c->atom;
}
BOOL UnregisterClass(PSZ name, ULONG instance)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Open32Class *c = find_class(instance, name);
    if (!c || c->references)
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(c ? 1412 : 1411);
        return FALSE;
    }
    Open32Class **link = &classes;
    while (*link != c)
        link = &(*link)->next;
    *link = c->next;
    lx_host_monitor_unlock(monitor);
    destroy_class(c);
    return TRUE;
}
BOOL GetClassInfo(ULONG instance, PSZ name, PVOID output)
{
    if (!output)
    {
        open32SetError(87);
        return FALSE;
    }
    Open32Class *c = open32ClassAcquire(instance, name);
    if (!c)
    {
        open32SetError(1411);
        return FALSE;
    }
    uint32 names = lx_execution_mapping_address(c->names);
    uint32 fields[] = {c->style,
                       c->procedure,
                       c->class_extra,
                       c->window_extra,
                       c->instance,
                       c->icon,
                       c->cursor,
                       c->brush,
                       c->menu ? names + 256 : c->menu_id,
                       names};
    for (unsigned i = 0; i < 10; i++)
        lx_guest_store_u32((uint8 *)output + i * 4, fields[i]);
    open32ClassRelease(c);
    return TRUE;
}

void open32ClassesCleanup(void)
{
    while (classes)
    {
        Open32Class *c = classes;
        classes = c->next;
        destroy_class(c);
    }
}
