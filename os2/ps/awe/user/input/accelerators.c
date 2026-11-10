/* Public domain. No rights reserved. Wholly AI-authored. */
/* Open32 accelerator handles own copied PM resources or Windows ACCEL arrays. They translate to
   Windows WM_COMMAND/WM_SYSCOMMAND messages, so this table is separate from PM's
   WinTranslateAccel, which posts PM commands with CMDSRC_ACCELERATOR. */
#include "os2/include/private/export.h"
#include "os2/include/bsedos.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/windows/windows.h"
#include "emulator/handles.h"
#include "host/sync.h"
#include <ctype.h>

/* PM ACCELTABLE resource: a USHORT count and code page, then 6-byte ACCEL entries of fs, key
   and cmd. */
enum
{
    ACCELTABLE_CODEPAGE_OFFSET = 2,
    ACCELTABLE_HEADER_SIZE = 4,
    ACCEL_SIZE = 6,
    ACCEL_KEY_OFFSET = 2,
    ACCEL_CMD_OFFSET = 4
};

enum
{
    ACCEL_KIND_MASK = AF_CHAR | AF_VIRTUALKEY | AF_SCANCODE,
    ACCEL_VALID_FLAGS =
        ACCEL_KIND_MASK | AF_SHIFT | AF_CONTROL | AF_ALT | AF_LONEKEY | AF_SYSCOMMAND | AF_HELP,
    WINDOWS_ACCEL_SHIFT_STATE = WIN32_FSHIFT | WIN32_FCONTROL | WIN32_FALT,
    WINDOWS_ACCEL_VALID_FLAGS = WIN32_FVIRTKEY | WIN32_FNOINVERT | WINDOWS_ACCEL_SHIFT_STATE,
    /* PM resources in these code pages convert to Windows Latin 1 key characters. */
    CODEPAGE_US = 437,
    CODEPAGE_MULTILINGUAL = 850,
    /* Windows key message lParam: scan code in bits 16-23, KF_* flags in the high word. */
    KEY_PARAMETER_SCAN_SHIFT = 16,
    KEY_PARAMETER_FLAGS_SHIFT = 16,
    KEY_STATE_DOWN = 0x8000,
    CHARACTER_ASCII_LIMIT = 0x80,
    /* WM_COMMAND/WM_SYSCOMMAND wParam high word 1 marks an accelerator as the source. */
    ACCELERATOR_COMMAND_SOURCE = 0x10000
};

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
    open32SetError(ERROR_NOT_ENOUGH_MEMORY);
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
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    return table->handle;
}

static AcceleratorTable *allocate_table(ULONG count)
{
    AcceleratorTable *table = calloc(1, sizeof *table + (size_t)count * sizeof *table->entries);
    if (!table)
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    else
        table->count = count;
    return table;
}

/* PM VK_* codes to Windows VK_* codes: a table below VK_F1, then the function key range. */
static ULONG windows_key(USHORT code)
{
    static const UCHAR keys[VK_F1] = {0,  1,    2,  4,  3,  8,  9,    9,    13, 16, 17,
                                      18, 0xa5, 19, 20, 27, 32, 33,   34,   35, 36, 37,
                                      38, 39,   40, 44, 45, 46, 0x91, 0x90, 13, 44};
    return code < VK_F1 ? keys[code] : code <= VK_F24 ? WIN32_VK_F1 + code - VK_F1 : 0;
}

ULONG CreateAcceleratorTable(PVOID data, LONG count)
{
    TRACE_NATIVE("CreateAcceleratorTable(%p, %d)", data, (int)count);
    if (!data || count <= 0 || count > INT16_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    AcceleratorTable *table = allocate_table((ULONG)count);
    if (!table)
        return 0;
    const UCHAR *wire = data;
    for (LONG i = 0; i < count; i++, wire += WIN32_ACCEL_SIZE)
    {
        if (wire[WIN32_ACCEL_FVIRT] & ~WINDOWS_ACCEL_VALID_FLAGS)
        {
            free(table);
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        table->entries[i] =
            (AcceleratorEntry){0, 0, lx_guest_load_u16(wire + WIN32_ACCEL_CMD),
                               wire[WIN32_ACCEL_FVIRT], lx_guest_load_u16(wire + WIN32_ACCEL_KEY)};
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
    if (!id || id > UINT16_MAX || !GProcessState.queryResourceSize || !GProcessState.getResource)
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    if ((!module || module == UINT32_MAX) && GProcessState.main_module)
        module = GProcessState.main_module->handle;
    ULONG size = 0;
    void *resource = NULL;
    if (GProcessState.queryResourceSize(module, RT_ACCELTABLE, (ULONG)id, &size) ||
        size < ACCELTABLE_HEADER_SIZE ||
        GProcessState.getResource(module, RT_ACCELTABLE, (ULONG)id, &resource))
    {
        open32SetError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return 0;
    }
    ULONG count = lx_guest_load_u16(resource),
          cp = lx_guest_load_u16((UCHAR *)resource + ACCELTABLE_CODEPAGE_OFFSET);
    ULONG character_cp = cp ? cp : GetOEMCP();
    AcceleratorTable *table = NULL;
    if ((size_t)count * ACCEL_SIZE <= size - ACCELTABLE_HEADER_SIZE &&
        (!cp || cp == CODEPAGE_US || cp == CODEPAGE_MULTILINGUAL || cp == WIN32_CP_WINANSI))
    {
        table = allocate_table(count);
        if (!table)
        {
            if (GProcessState.freeResource)
                GProcessState.freeResource(resource);
            return 0;
        }
    }
    if (table)
    {
        table->native = TRUE;
        const UCHAR *entry = (const UCHAR *)resource + ACCELTABLE_HEADER_SIZE;
        for (ULONG i = 0; i < count; i++, entry += ACCEL_SIZE)
        {
            USHORT flags = lx_guest_load_u16(entry),
                   key = lx_guest_load_u16(entry + ACCEL_KEY_OFFSET);
            if (!(flags & ACCEL_KIND_MASK) || (flags & ~ACCEL_VALID_FLAGS))
            {
                free(table);
                table = NULL;
                break;
            }
            UCHAR win_flags = (flags & AF_SHIFT ? WIN32_FSHIFT : 0) |
                              (flags & AF_CONTROL ? WIN32_FCONTROL : 0) |
                              (flags & AF_ALT ? WIN32_FALT : 0);
            ULONG converted = flags & AF_VIRTUALKEY ? windows_key(key) : 0;
            if (converted)
                win_flags |= WIN32_FVIRTKEY;
            else
                converted = character_cp != WIN32_CP_WINANSI && key < 256
                                ? WinCpTranslateChar(0, character_cp, (UCHAR)key, WIN32_CP_WINANSI)
                                : key;
            table->entries[i] =
                (AcceleratorEntry){flags, key, lx_guest_load_u16(entry + ACCEL_CMD_OFFSET),
                                   win_flags, (USHORT)converted};
        }
    }
    if (GProcessState.freeResource)
        GProcessState.freeResource(resource);
    if (!table)
    {
        open32SetError(ERROR_INVALID_DATA);
        return 0;
    }
    return retain_table(table);
}

LONG CopyAcceleratorTable(ULONG handle, PVOID data, LONG count)
{
    LxHandleBorrow borrow = {0};
    if (count < 0 || !lx_handle_borrow(handle, LX_HANDLE_OPEN32_ACCEL, &borrow))
    {
        open32SetError(count < 0 ? ERROR_INVALID_PARAMETER : ERROR_INVALID_ACCEL_HANDLE);
        return 0;
    }
    AcceleratorTable *table = borrow.resource;
    ULONG copied = !data || (ULONG)count > table->count ? table->count : (ULONG)count;
    if (data)
        for (ULONG i = 0; i < copied; i++)
        {
            UCHAR *wire = (UCHAR *)data + i * WIN32_ACCEL_SIZE;
            wire[WIN32_ACCEL_FVIRT] = table->entries[i].windows_flags;
            wire[WIN32_ACCEL_FVIRT + 1] = 0;
            lx_guest_store_u16(wire + WIN32_ACCEL_KEY, table->entries[i].windows_key);
            lx_guest_store_u16(wire + WIN32_ACCEL_CMD, table->entries[i].command);
        }
    lx_handle_release(&borrow);
    return (LONG)copied;
}

BOOL DestroyAcceleratorTable(ULONG handle)
{
    if (lx_handle_close(handle, LX_HANDLE_OPEN32_ACCEL))
        return TRUE;
    open32SetError(ERROR_INVALID_ACCEL_HANDLE);
    return FALSE;
}

static BOOL matches(const AcceleratorEntry *entry, BOOL native, ULONG message, ULONG key,
                    ULONG parameter, ULONG modifiers)
{
    BOOL down = message == WIN32_WM_KEYDOWN || message == WIN32_WM_SYSKEYDOWN;
    BOOL character = message == WIN32_WM_CHAR || message == WIN32_WM_SYSCHAR;
    if (!native)
        return (entry->windows_flags & WINDOWS_ACCEL_SHIFT_STATE) == modifiers &&
               ((entry->windows_flags & WIN32_FVIRTKEY) ? down : character) &&
               key == entry->windows_key;
    if ((entry->windows_flags & WINDOWS_ACCEL_SHIFT_STATE) != modifiers)
        return FALSE;
    if (down && (entry->flags & AF_VIRTUALKEY) && windows_key(entry->key) &&
        key == windows_key(entry->key))
        return TRUE;
    if (down && (entry->flags & AF_SCANCODE) &&
        ((parameter >> KEY_PARAMETER_SCAN_SHIFT) & 255) == entry->key)
        return TRUE;
    if (!(entry->flags & AF_CHAR))
        return FALSE;
    if (character && key == entry->windows_key)
        return TRUE;
    /* PM resources also spell virtual letter accelerators as CHAR|VIRTUALKEY. */
    return down && key < CHARACTER_ASCII_LIMIT && entry->key < CHARACTER_ASCII_LIMIT &&
           toupper((int)key) == toupper((int)entry->key);
}

LONG TranslateAccelerator(ULONG window, ULONG handle, PVOID message)
{
    TRACE_NATIVE("TranslateAccelerator(%u, %u, %p)", (uint)window, (uint)handle, message);
    if (!message)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG kind = lx_guest_load_u32((UCHAR *)message + WIN32_MSG_MESSAGE);
    if (kind != WIN32_WM_KEYDOWN && kind != WIN32_WM_CHAR && kind != WIN32_WM_SYSKEYDOWN &&
        kind != WIN32_WM_SYSCHAR)
        return 0;
    if (!IsWindow(window))
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    LxHandleBorrow borrow = {0};
    if (!lx_handle_borrow(handle, LX_HANDLE_OPEN32_ACCEL, &borrow))
    {
        open32SetError(ERROR_INVALID_ACCEL_HANDLE);
        return 0;
    }
    AcceleratorTable *table = borrow.resource;
    ULONG key = lx_guest_load_u32((UCHAR *)message + WIN32_MSG_WPARAM);
    ULONG parameter = lx_guest_load_u32((UCHAR *)message + WIN32_MSG_LPARAM);
    ULONG modifiers = (GetKeyState(WIN32_VK_SHIFT) & KEY_STATE_DOWN ? WIN32_FSHIFT : 0) |
                      (GetKeyState(WIN32_VK_CONTROL) & KEY_STATE_DOWN ? WIN32_FCONTROL : 0) |
                      ((GetKeyState(WIN32_VK_MENU) & KEY_STATE_DOWN) ||
                               (parameter & ((ULONG)WIN32_KF_ALTDOWN << KEY_PARAMETER_FLAGS_SHIFT))
                           ? WIN32_FALT
                           : 0);
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
    if (match.flags & AF_HELP)
    {
        open32SetError(ERROR_NOT_SUPPORTED);
        return 0;
    }
    /* Drop the table lease before invoking original code: it may destroy it. */
    SendMessage(window, match.flags & AF_SYSCOMMAND ? WIN32_WM_SYSCOMMAND : WIN32_WM_COMMAND,
                ACCELERATOR_COMMAND_SOURCE | match.command, 0);
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
