/* Public domain. No rights reserved. Wholly AI-authored. */
/* Accelerator tables translate a QMSG in place and retain their packed resource bytes. */
#include "os2/include/private/pmwinp.h"
#include "emulator/abi.h"
#include <ctype.h>

/* Resource type (the Toolkit's bsedos.h). */
enum
{
    RT_ACCELTABLE = 8
};

/* ACCELTABLE: a USHORT count and code page, then 6-byte ACCEL entries of fs, key and cmd. */
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
    ACCEL_SHIFT_MASK = AF_SHIFT | AF_CONTROL | AF_ALT,
    ACCEL_VALID_FLAGS = ACCEL_KIND_MASK | ACCEL_SHIFT_MASK | AF_LONEKEY | AF_SYSCOMMAND | AF_HELP
};

typedef struct PmAccelerator
{
    struct PmAccelerator *next;
    AnchorBlock *anchor;
    ULONG handle;
    unsigned users, count;
    size_t size;
    uint8 data[];
} PmAccelerator;

static void destroy_accelerator(void *resource)
{
    PmAccelerator *table = resource;
    PmAccelerator **slot = &table->anchor->accelerators;
    while (*slot && *slot != table)
        slot = &(*slot)->next;
    if (*slot)
        *slot = table->next;
    free(table);
}

static PmAccelerator *borrow_table(ULONG handle, LxHandleBorrow *borrow)
{
    AnchorBlock *anchor = getAnchorBlockNoHAB();
    if (!anchor || !lx_handle_borrow(handle, LX_HANDLE_PM_ACCEL, borrow))
        return NULL;
    PmAccelerator *table = borrow->resource;
    if (table->anchor != anchor)
    {
        lx_handle_release(borrow);
        return NULL;
    }
    return table;
}

ULONG WinCreateAccelTable(HAB hab, PVOID data)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    if (!anchor || !data)
        return 0;
    const uint8 *p = data;
    unsigned count = lx_guest_load_u16(p), cp = lx_guest_load_u16(p + ACCELTABLE_CODEPAGE_OFFSET);
    if (cp && cp != 437 && cp != 850)
        return 0;
    size_t size = ACCELTABLE_HEADER_SIZE + (size_t)count * ACCEL_SIZE;
    for (unsigned i = 0; i < count; i++)
    {
        unsigned flags = lx_guest_load_u16(p + ACCELTABLE_HEADER_SIZE + i * ACCEL_SIZE);
        unsigned kind = flags & ACCEL_KIND_MASK;
        if ((flags & ~(unsigned)ACCEL_VALID_FLAGS) ||
            (kind != AF_CHAR && kind != AF_VIRTUALKEY && kind != AF_SCANCODE))
            return 0;
    }
    PmAccelerator *table = calloc(1, sizeof *table + size);
    if (!table)
        return 0;
    table->anchor = anchor;
    table->size = size;
    table->count = count;
    memcpy(table->data, data, size);
    table->handle = lx_handle_create(LX_HANDLE_PM_ACCEL, table, destroy_accelerator);
    if (!table->handle)
    {
        free(table);
        return 0;
    }
    table->next = anchor->accelerators;
    anchor->accelerators = table;
    return table->handle;
}

ULONG WinLoadAccelTable(HAB hab, HMODULE module, ULONG id)
{
    if (!getAnchorBlock(hab) || !GProcessState.getResource || !GProcessState.queryResourceSize)
        return 0;
    uint32 size = 0;
    void *resource = NULL;
    if (GProcessState.queryResourceSize(module, RT_ACCELTABLE, id, &size) ||
        size < ACCELTABLE_HEADER_SIZE ||
        GProcessState.getResource(module, RT_ACCELTABLE, id, &resource))
        return 0;
    unsigned count = lx_guest_load_u16(resource);
    ULONG result = (size_t)count * ACCEL_SIZE <= size - ACCELTABLE_HEADER_SIZE
                       ? WinCreateAccelTable(hab, resource)
                       : 0;
    if (GProcessState.freeResource)
        GProcessState.freeResource(resource);
    return result;
}

ULONG WinCopyAccelTable(ULONG handle, PVOID data, ULONG size)
{
    LxHandleBorrow borrow = {0};
    PmAccelerator *table = borrow_table(handle, &borrow);
    ULONG result = table ? (ULONG)table->size : 0;
    if (table && size && data)
        memcpy(data, table->data, size < result ? size : result);
    else if (size && !data)
        result = 0;
    lx_handle_release(&borrow);
    return result;
}

static int unused_accelerator(void *resource)
{
    return ((PmAccelerator *)resource)->users == 0;
}

BOOL WinDestroyAccelTable(ULONG handle)
{
    LxHandleBorrow borrow = {0};
    PmAccelerator *table = borrow_table(handle, &borrow);
    BOOL result =
        table && lx_handle_close_guarded(handle, LX_HANDLE_PM_ACCEL, 1, unused_accelerator) == 1;
    lx_handle_release(&borrow);
    return result;
}

void releaseWindowAccelerator(Window *window)
{
    if (window->accelerator.resource)
        ((PmAccelerator *)window->accelerator.resource)->users--;
    lx_handle_release(&window->accelerator);
}

void destroyAccelerators(AnchorBlock *anchor)
{
    while (anchor->accelerators)
        lx_handle_close(anchor->accelerators->handle, LX_HANDLE_PM_ACCEL);
}

BOOL WinSetAccelTable(HAB hab, ULONG handle, HWND frame)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    Window *window = anchor ? getWindowFromHWND(anchor, frame) : NULL;
    if (!window)
        return FALSE;
    LxHandleBorrow borrow = {0};
    if (handle)
    {
        PmAccelerator *table = borrow_table(handle, &borrow);
        if (!table)
            return FALSE;
        table->users++;
    }
    releaseWindowAccelerator(window);
    window->accelerator = borrow;
    return TRUE;
}

ULONG WinQueryAccelTable(HAB hab, HWND frame)
{
    AnchorBlock *anchor = getAnchorBlock(hab);
    Window *window = anchor ? getWindowFromHWND(anchor, frame) : NULL;
    PmAccelerator *table = window ? window->accelerator.resource : NULL;
    return table ? table->handle : 0;
}

BOOL WinTranslateAccel(HAB hab, HWND frame, ULONG handle, PQMSG message)
{
    TRACE_NATIVE("WinTranslateAccel(%u, %u, %u, %p)", (uint)hab, (uint)frame, (uint)handle,
                 message);
    if (!getAnchorBlock(hab) || !message || message->msg != WM_CHAR)
        return FALSE;
    if (!handle)
        handle = WinQueryAccelTable(hab, frame);
    LxHandleBorrow borrow = {0};
    PmAccelerator *table = borrow_table(handle, &borrow);
    BOOL result = FALSE;
    ULONG key_flags = (ULONG)(uintptr_t)message->mp1, key = (ULONG)(uintptr_t)message->mp2;
    if (!table || (key_flags & KC_KEYUP))
        goto done;
    for (unsigned i = 0; i < table->count; i++)
    {
        const uint8 *entry = table->data + ACCELTABLE_HEADER_SIZE + i * ACCEL_SIZE;
        unsigned flags = lx_guest_load_u16(entry),
                 code = lx_guest_load_u16(entry + ACCEL_KEY_OFFSET);
        /* The AF_ shift-state bits have the KC_ values. */
        if ((flags & ACCEL_SHIFT_MASK) != (key_flags & (KC_SHIFT | KC_CTRL | KC_ALT)))
            continue;
        BOOL match = FALSE;
        /* Character keys match without regard to ASCII case. */
        if ((flags & AF_CHAR) && (key_flags & KC_CHAR))
            match = code == (key & 0xffff) || (code < 128 && (key & 0xffff) < 128 &&
                                               toupper((int)code) == toupper((int)(key & 0xffff)));
        else if ((flags & AF_VIRTUALKEY) && (key_flags & KC_VIRTUALKEY))
            match = code == key >> 16;
        else if ((flags & AF_SCANCODE) && (key_flags & KC_SCANCODE))
            match = code == (key_flags >> 24);
        if (!match)
            continue;
        message->hwnd = frame;
        message->msg = flags & AF_SYSCOMMAND ? WM_SYSCOMMAND
                       : flags & AF_HELP     ? WM_HELP
                                             : WM_COMMAND;
        message->mp1 = (MPARAM)(uintptr_t)lx_guest_load_u16(entry + ACCEL_CMD_OFFSET);
        message->mp2 = (MPARAM)(uintptr_t)CMDSRC_ACCELERATOR;
        result = TRUE;
        break;
    }
done:
    lx_handle_release(&borrow);
    return result;
}
