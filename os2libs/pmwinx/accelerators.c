/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 accelerator handles own copied PM resources or Windows ACCEL arrays. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "os2libs/pmwinx/windows.h"
#include "guest/handles.h"
#include "hal/host_sync.h"
#include <ctype.h>
typedef struct AcceleratorEntry
{
    USHORT flags, key, command;
    UCHAR windows_flags;
    USHORT windows_key;
} AcceleratorEntry;
typedef struct AcceleratorTable
{
    struct AcceleratorTable *next;
    ULONG handle, count;
    BOOL native;
    AcceleratorEntry entries[];
} AcceleratorTable;
static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static AcceleratorTable *tables;
static void initialize(void)
{
    monitor = lx_host_monitor_create();
}
static BOOL ready(void)
{
    lx_host_once(&once, initialize);
    if (monitor)
        return TRUE;
    open32SetError(8);
    return FALSE;
}
static void destroy_table(void *resource)
{
    AcceleratorTable *table = resource;
    lx_host_monitor_lock(monitor);
    AcceleratorTable **slot = &tables;
    while (*slot && *slot != table)
        slot = &(*slot)->next;
    if (*slot)
        *slot = table->next;
    lx_host_monitor_unlock(monitor);
    free(table);
}
static ULONG retain_table(AcceleratorTable *table)
{
    if (!ready())
    {
        free(table);
        return 0;
    }
    lx_host_monitor_lock(monitor);
    table->handle = lx_handle_create(LX_HANDLE_OPEN32_ACCEL, table, destroy_table);
    if (table->handle)
    {
        table->next = tables;
        tables = table;
    }
    lx_host_monitor_unlock(monitor);
    if (!table->handle)
    {
        free(table);
        open32SetError(8);
        return 0;
    }
    return table->handle;
}
static AcceleratorTable *allocate_table(ULONG count)
{
    AcceleratorTable *table = calloc(1, sizeof *table + (size_t)count * sizeof *table->entries);
    if (!table)
        open32SetError(8);
    else
        table->count = count;
    return table;
}
static ULONG windows_key(USHORT code)
{
    static const UCHAR keys[32] = {0,  1,    2,  4,  3,  8,  9,    9,    13, 16, 17,
                                   18, 0xa5, 19, 20, 27, 32, 33,   34,   35, 36, 37,
                                   38, 39,   40, 44, 45, 46, 0x91, 0x90, 13, 44};
    return code < 32 ? keys[code] : code <= 0x37 ? 0x70 + code - 0x20 : 0;
}
ULONG CreateAcceleratorTable(PVOID data, LONG count)
{
    TRACE_NATIVE("CreateAcceleratorTable(%p, %d)", data, (int)count);
    if (!data || count <= 0 || count > 32767)
    {
        open32SetError(87);
        return 0;
    }
    AcceleratorTable *table = allocate_table((ULONG)count);
    if (!table)
        return 0;
    const UCHAR *wire = data;
    for (LONG i = 0; i < count; i++, wire += 6)
    {
        if (wire[0] & ~0x1f)
        {
            free(table);
            open32SetError(87);
            return 0;
        }
        table->entries[i] = (AcceleratorEntry){0, 0, lx_guest_load_u16(wire + 4), wire[0],
                                               lx_guest_load_u16(wire + 2)};
    }
    return retain_table(table);
}
ULONG LoadAccelerators(HMODULE module, PSZ name)
{
    TRACE_NATIVE("LoadAccelerators(%u, %p)", (uint)module, name);
    uintptr_t id = (uintptr_t)name;
    if (id > UINT16_MAX && name && name[0] == '#')
    {
        char *end;
        unsigned long number = strtoul(name + 1, &end, 10);
        id = *end || number > UINT16_MAX ? 0 : number;
    }
    if (!id || id > UINT16_MAX || !GLoaderState.queryResourceSize || !GLoaderState.getResource)
    {
        open32SetError(1814);
        return 0;
    }
    if ((!module || module == UINT32_MAX) && GLoaderState.main_module)
        module = GLoaderState.main_module->handle;
    ULONG size = 0;
    void *resource = NULL;
    if (GLoaderState.queryResourceSize(module, 8, (ULONG)id, &size) || size < 4 ||
        GLoaderState.getResource(module, 8, (ULONG)id, &resource))
    {
        open32SetError(1814);
        return 0;
    }
    ULONG count = lx_guest_load_u16(resource), cp = lx_guest_load_u16((UCHAR *)resource + 2);
    ULONG character_cp = cp ? cp : GetOEMCP();
    AcceleratorTable *table = NULL;
    if ((size_t)count * 6 <= size - 4 && (!cp || cp == 437 || cp == 850 || cp == 1004))
    {
        table = allocate_table(count);
        if (!table)
        {
            if (GLoaderState.freeResource)
                GLoaderState.freeResource(resource);
            return 0;
        }
    }
    if (table)
    {
        table->native = TRUE;
        const UCHAR *entry = (const UCHAR *)resource + 4;
        for (ULONG i = 0; i < count; i++, entry += 6)
        {
            USHORT flags = lx_guest_load_u16(entry), key = lx_guest_load_u16(entry + 2);
            if (!(flags & 7) || (flags & ~0x37f))
            {
                free(table);
                table = NULL;
                break;
            }
            UCHAR win_flags = (flags & 8 ? 4 : 0) | (flags & 16 ? 8 : 0) | (flags & 32 ? 16 : 0);
            ULONG converted = flags & 2 ? windows_key(key) : 0;
            if (converted)
                win_flags |= 1;
            else
                converted = character_cp != 1004 && key < 256
                                ? WinCpTranslateChar(0, character_cp, (UCHAR)key, 1004)
                                : key;
            table->entries[i] = (AcceleratorEntry){flags, key, lx_guest_load_u16(entry + 4),
                                                   win_flags, (USHORT)converted};
        }
    }
    if (GLoaderState.freeResource)
        GLoaderState.freeResource(resource);
    if (!table)
    {
        open32SetError(13);
        return 0;
    }
    return retain_table(table);
}
LONG CopyAcceleratorTable(ULONG handle, PVOID data, LONG count)
{
    LxHandleBorrow borrow = {0};
    if (count < 0 || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_ACCEL, &borrow))
    {
        open32SetError(count < 0 ? 87 : 1403);
        return 0;
    }
    AcceleratorTable *table = borrow.resource;
    ULONG copied = !data || (ULONG)count > table->count ? table->count : (ULONG)count;
    if (data)
        for (ULONG i = 0; i < copied; i++)
        {
            UCHAR *wire = (UCHAR *)data + i * 6;
            wire[0] = table->entries[i].windows_flags;
            wire[1] = 0;
            lx_guest_store_u16(wire + 2, table->entries[i].windows_key);
            lx_guest_store_u16(wire + 4, table->entries[i].command);
        }
    lx_handle_release(&borrow);
    return (LONG)copied;
}
BOOL DestroyAcceleratorTable(ULONG handle)
{
    if (lx_handle_close(handle, LX_HANDLE_OPEN32_ACCEL))
        return TRUE;
    open32SetError(1403);
    return FALSE;
}
static BOOL matches(const AcceleratorEntry *entry, BOOL native, ULONG message, ULONG key,
                    ULONG parameter, ULONG modifiers)
{
    BOOL down = message == 0x100 || message == 0x104;
    BOOL character = message == 0x102 || message == 0x106;
    if (!native)
        return (entry->windows_flags & 0x1c) == modifiers &&
               ((entry->windows_flags & 1) ? down : character) && key == entry->windows_key;
    if ((entry->windows_flags & 0x1c) != modifiers)
        return FALSE;
    if (down && (entry->flags & 2) && windows_key(entry->key) && key == windows_key(entry->key))
        return TRUE;
    if (down && (entry->flags & 4) && ((parameter >> 16) & 255) == entry->key)
        return TRUE;
    if (!(entry->flags & 1))
        return FALSE;
    if (character && key == entry->windows_key)
        return TRUE;
    /* PM resources also spell virtual letter accelerators as CHAR|VIRTUALKEY. */
    return down && key < 128 && entry->key < 128 && toupper((int)key) == toupper((int)entry->key);
}
LONG TranslateAccelerator(ULONG window, ULONG handle, PVOID message)
{
    TRACE_NATIVE("TranslateAccelerator(%u, %u, %p)", (uint)window, (uint)handle, message);
    if (!message)
    {
        open32SetError(87);
        return 0;
    }
    ULONG kind = lx_guest_load_u32((UCHAR *)message + 4);
    if (kind != 0x100 && kind != 0x102 && kind != 0x104 && kind != 0x106)
        return 0;
    if (!IsWindow(window))
    {
        open32SetError(1400);
        return 0;
    }
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_ACCEL, &borrow))
    {
        open32SetError(1403);
        return 0;
    }
    AcceleratorTable *table = borrow.resource;
    ULONG key = lx_guest_load_u32((UCHAR *)message + 8);
    ULONG parameter = lx_guest_load_u32((UCHAR *)message + 12);
    ULONG modifiers = (GetKeyState(16) & 0x8000 ? 4 : 0) | (GetKeyState(17) & 0x8000 ? 8 : 0) |
                      ((GetKeyState(18) & 0x8000) || (parameter & (1u << 29)) ? 16 : 0);
    AcceleratorEntry match = {0};
    BOOL found = FALSE;
    for (ULONG i = 0; i < table->count; i++)
        if (matches(&table->entries[i], table->native, kind, key, parameter, modifiers))
        {
            match = table->entries[i];
            found = TRUE;
            break;
        }
    lx_handle_release(&borrow);
    if (!found || !IsWindowEnabled(window))
        return 0;
    if (match.flags & 0x200)
    {
        open32SetError(50);
        return 0;
    }
    /* Drop the table lease before invoking original code: it may destroy it. */
    SendMessage(window, match.flags & 0x100 ? 0x112 : 0x111, 0x10000 | match.command, 0);
    return 1;
}
void open32AcceleratorsCleanup(void)
{
    /* Native-module shutdown has no active callers or outstanding table leases. */
    while (tables)
        lx_handle_close(tables->handle, LX_HANDLE_OPEN32_ACCEL);
    if (monitor)
        lx_host_monitor_destroy(monitor);
    monitor = NULL;
    once = (LxHostOnce)LX_HOST_ONCE_INIT;
}
