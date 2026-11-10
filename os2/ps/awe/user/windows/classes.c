/* Public domain. No rights reserved. Wholly AI-authored. */
/* Registered Open32 classes own callback metadata, extra bytes and copied names. */
#include "os2/ps/awe/user/windows/windows.h"
#include "os2/ps/awe/internal.h"
#include "host/sync.h"
#include "os2/include/private/pmgrep.h"

enum
{
    /* Windows class atoms are string atoms: 0xC000 to 0xFFFF. */
    CLASS_ATOM_FIRST = 0xC000,
    /* Longest class or menu name copied from the guest, including its terminator. */
    CLASS_NAME_LIMIT = 256,
    /* The class's guest names page holds the class name, then the menu name here. */
    NAMES_MENU_OFFSET = 256,
    /* Most class or window extra bytes a class may ask for. */
    EXTRA_BYTES_LIMIT = 4096,
    /* A window procedure takes handle, message, wParam and lParam. */
    WINDOW_PROCEDURE_ARGUMENT_BYTES = 16,
    /* A dialog control's low style word is its class's own style bits in both systems. */
    CONTROL_STYLE_MASK = 0xFFFF
};

static LxHostOnce once = LX_HOST_ONCE_INIT;
static LxHostMonitor *monitor;
static Open32Class *classes;
static ULONG next_atom = CLASS_ATOM_FIRST;

static void initialize(void)
{
    monitor = lx_host_monitor_create();
    if (!monitor)
        abort();
}

static Open32Class *find_class(ULONG instance, PSZ name)
{
    for (Open32Class *c = classes; c; c = c->next)
        if ((c->instance == instance || (c->style & WIN32_CS_GLOBALCLASS)) &&
            ((uintptr_t)name <= UINT16_MAX ? c->atom == (ULONG)(uintptr_t)name
                                           : !strcasecmp(c->name, name)))
            return c;
    return NULL;
}

static uint32 builtin_procedure(uint8 *arguments)
{
    ULONG handle = lx_guest_load_u32(arguments), message = lx_guest_load_u32(arguments + 4);
    ULONG first = lx_guest_load_u32(arguments + 8), second = lx_guest_load_u32(arguments + 12);
    /* A superclass chains here for the default behaviour of its PM control. */
    if (!open32FindWindow(handle) && open32NativeDialogWindow(handle))
        return (uint32)open32NativeControlMessage(handle, message, first, second, TRUE);
    return open32BuiltinWindowProc(handle, message, first, second);
}

static Open32Class *builtin_class(const char *name)
{
    static const char *const controls[] = {"BUTTON",  "COMBOBOX", "EDIT",
                                           "LISTBOX", "MLE",      "STATIC"};
    const char *canonical = NULL;
    for (size_t i = 0; i < sizeof controls / sizeof *controls; i++)
        if (!strcasecmp(name, controls[i]))
            canonical = controls[i];
    BOOL scroll = !strcasecmp(name, "SCROLLBAR");
    if (!canonical && !scroll && strcasecmp(name, "MDICLIENT"))
        return NULL;
    Open32Class *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->name = strdup(canonical ? canonical : scroll ? "SCROLLBAR" : "MDICLIENT");
    c->style = WIN32_CS_GLOBALCLASS | WIN32_CS_DBLCLKS;
    c->builtin = canonical ? OPEN32_BUILTIN_CONTROL
                 : scroll  ? OPEN32_BUILTIN_SCROLLBAR
                           : OPEN32_BUILTIN_MDI_CLIENT;
    /* A class brush may be a system colour index plus one. */
    c->brush = canonical ? 0 : scroll ? WIN32_COLOR_SCROLLBAR + 1 : WIN32_COLOR_APPWORKSPACE + 1;
    c->names = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                LX_GUEST_READ | LX_GUEST_WRITE, 0);
    c->procedure_mapping = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                            LX_GUEST_READ | LX_GUEST_WRITE, 0);
    const LxExecutionApi32 bridge = {builtin_procedure, WINDOW_PROCEDURE_ARGUMENT_BYTES, 0,
                                     LX_ABI_STACK32};
    if (!c->name || !c->names || !c->procedure_mapping ||
        !lx_execution_emit_bridge32(GProcessState.execution,
                                    lx_execution_mapping_host(c->procedure_mapping),
                                    OPEN32_PAGE_SIZE, &bridge))
        goto fail;
    strcpy(lx_execution_mapping_host(c->names), c->name);
    if (!lx_execution_protect(GProcessState.execution, c->names, LX_GUEST_READ) ||
        !lx_execution_protect(GProcessState.execution, c->procedure_mapping,
                              LX_GUEST_READ | LX_GUEST_EXECUTE))
        goto fail;
    c->procedure = lx_execution_mapping_address(c->procedure_mapping);
    return c;
fail:
    if (c->names)
        lx_execution_unmap(GProcessState.execution, c->names);
    if (c->procedure_mapping)
        lx_execution_unmap(GProcessState.execution, c->procedure_mapping);
    free(c->name);
    free(c);
    return NULL;
}

Open32Class *open32ClassAcquire(ULONG instance, PSZ name)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Open32Class *c = find_class(instance, name);
    if (!c && (uintptr_t)name > UINT16_MAX && next_atom <= UINT16_MAX)
    {
        c = builtin_class(name);
        if (c)
        {
            c->atom = next_atom++;
            c->next = classes;
            classes = c;
        }
    }
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
        lx_execution_unmap(GProcessState.execution, c->names);
    if (c->procedure_mapping)
        lx_execution_unmap(GProcessState.execution, c->procedure_mapping);
    free(c->class_data);
    free(c->menu);
    free(c->name);
    free(c);
}

/* A dialog template names a class the application registered through this API.
   PM coordinates and styles become their Windows forms for CreateWindowEx. */
static HWND dialog_control(HWND parent, const char *name, const char *text, ULONG style, LONG x,
                           LONG y, LONG width, LONG height, ULONG id)
{
    RECTL area;
    if (!WinQueryWindowRect(parent, &area))
        return 0;
    ULONG windows_style =
        (ULONG)WIN32_WS_CHILD | (style & WS_VISIBLE ? (ULONG)WIN32_WS_VISIBLE : 0) |
        (style & WS_DISABLED ? (ULONG)WIN32_WS_DISABLED : 0) | (style & CONTROL_STYLE_MASK);
    return CreateWindowEx(0, (PSZ)name, (PSZ)text, windows_style, x,
                          area.yTop - area.yBottom - y - height, width, height, parent, id,
                          UINT32_MAX, 0);
}

ULONG RegisterClass(PVOID record)
{
    if (!record || !GProcessState.execution)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const uint8 *bytes = record;
    Open32Class *c = calloc(1, sizeof *c);
    if (!c)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    c->style = lx_guest_load_u32(bytes + WIN32_WNDCLASS_STYLE);
    c->procedure = lx_guest_load_u32(bytes + WIN32_WNDCLASS_PROCEDURE);
    c->class_extra = lx_guest_load_u32(bytes + WIN32_WNDCLASS_CLASS_EXTRA);
    c->window_extra = lx_guest_load_u32(bytes + WIN32_WNDCLASS_WINDOW_EXTRA);
    c->instance = lx_guest_load_u32(bytes + WIN32_WNDCLASS_INSTANCE);
    c->icon = lx_guest_load_u32(bytes + WIN32_WNDCLASS_ICON);
    c->cursor = lx_guest_load_u32(bytes + WIN32_WNDCLASS_CURSOR);
    c->brush = lx_guest_load_u32(bytes + WIN32_WNDCLASS_BACKGROUND);
    c->menu_id = lx_guest_load_u32(bytes + WIN32_WNDCLASS_MENU_NAME);
    c->name = lx_execution_copy_string(GProcessState.execution,
                                       lx_guest_load_u32(bytes + WIN32_WNDCLASS_CLASS_NAME),
                                       CLASS_NAME_LIMIT);
    /* A menu "name" up to 0xFFFF is a resource number (MAKEINTRESOURCE). */
    if (c->menu_id > UINT16_MAX)
        c->menu = lx_execution_copy_string(GProcessState.execution, c->menu_id, CLASS_NAME_LIMIT);
    if (!c->name || !*c->name || c->class_extra > EXTRA_BYTES_LIMIT ||
        c->window_extra > EXTRA_BYTES_LIMIT || (c->menu_id > UINT16_MAX && !c->menu) ||
        !open32ValidProcedure(c->procedure))
    {
        destroy_class(c);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (c->class_extra && !(c->class_data = calloc(1, c->class_extra)))
    {
        destroy_class(c);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    c->names = lx_execution_map(GProcessState.execution, 0, OPEN32_PAGE_SIZE,
                                LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!c->names)
    {
        destroy_class(c);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    char *names = lx_execution_mapping_host(c->names);
    strcpy(names, c->name);
    if (c->menu)
        strcpy(names + NAMES_MENU_OFFSET, c->menu);
    if (!lx_execution_protect(GProcessState.execution, c->names, LX_GUEST_READ))
    {
        destroy_class(c);
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    if (find_class(c->instance, c->name) || next_atom > UINT16_MAX)
    {
        lx_host_monitor_unlock(monitor);
        destroy_class(c);
        open32SetError(ERROR_CLASS_ALREADY_EXISTS);
        return 0;
    }
    c->atom = next_atom++;
    c->next = classes;
    classes = c;
    lx_host_monitor_unlock(monitor);
    lx_pm_set_dialog_class_factory(dialog_control);
    TRACE_NATIVE("RegisterClass('%s', instance=%u, proc=%X) -> %u", c->name, c->instance,
                 c->procedure, c->atom);
    return c->atom;
}

BOOL RegisterClassAlias(PSZ name, PSZ alias)
{
    if ((uintptr_t)name <= UINT16_MAX || (uintptr_t)alias <= UINT16_MAX)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    TRACE_NATIVE("RegisterClassAlias('%s', '%s')", name, alias);
    Open32Class *predefined = open32ClassAcquire(UINT32_MAX, alias);
    if (predefined)
        open32ClassRelease(predefined);
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Open32Class *source = NULL, *target = NULL;
    for (Open32Class *c = classes; c; c = c->next)
    {
        if (!source && !strcasecmp(c->name, name))
            source = c;
        if (!target && !strcasecmp(c->name, alias))
            target = c;
    }
    if (source && target)
        source->alias = target;
    lx_host_monitor_unlock(monitor);
    if (!source || !target)
        open32SetError(ERROR_INVALID_PARAMETER);
    return source && target;
}

BOOL UnregisterClass(PSZ name, ULONG instance)
{
    lx_host_once(&once, initialize);
    lx_host_monitor_lock(monitor);
    Open32Class *c = find_class(instance, name);
    if (!c || c->references || c->builtin)
    {
        lx_host_monitor_unlock(monitor);
        open32SetError(c ? ERROR_CLASS_HAS_WINDOWS : ERROR_CLASS_DOES_NOT_EXIST);
        return FALSE;
    }
    Open32Class **link = &classes;
    while (*link != c)
        link = &(*link)->next;
    *link = c->next;
    for (Open32Class *other = classes; other; other = other->next)
        if (other->alias == c)
            other->alias = NULL;
    lx_host_monitor_unlock(monitor);
    destroy_class(c);
    return TRUE;
}

BOOL GetClassInfo(ULONG instance, PSZ name, PVOID output)
{
    TRACE_NATIVE("GetClassInfo(%u, %p)", instance, name);
    if (!output)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Open32Class *c = open32ClassAcquire(instance, name);
    if (!c)
    {
        open32SetError(ERROR_CLASS_DOES_NOT_EXIST);
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
                       c->menu ? names + NAMES_MENU_OFFSET : c->menu_id,
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

ULONG GetClassLong(ULONG handle, LONG index)
{
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    Open32Class *c = window->class;
    if (index >= 0)
    {
        if ((ULONG)index <= c->class_extra && c->class_extra - (ULONG)index >= 4)
            return lx_guest_load_u32((uint8 *)c->class_data + index);
        open32SetError(ERROR_INVALID_INDEX);
        return 0;
    }
    switch (index)
    {
        case WIN32_GCL_MENUNAME:
            return c->menu ? lx_execution_mapping_address(c->names) + NAMES_MENU_OFFSET
                           : c->menu_id;
        case WIN32_GCL_HBRBACKGROUND:
            return c->brush;
        case WIN32_GCL_HCURSOR:
            return c->cursor;
        case WIN32_GCL_HICON:
            return c->icon;
        case WIN32_GCL_HMODULE:
            return c->instance;
        case WIN32_GCL_CBWNDEXTRA:
            return c->window_extra;
        case WIN32_GCL_CBCLSEXTRA:
            return c->class_extra;
        case WIN32_GCL_WNDPROC:
            return c->procedure;
        case WIN32_GCL_STYLE:
            return c->style;
        case WIN32_GCW_ATOM:
            return c->atom;
        case WIN32_GCL_HICONSM:
            return c->small_icon;
        default:
            open32SetError(ERROR_INVALID_INDEX);
            return 0;
    }
}

ULONG SetClassLong(ULONG handle, LONG index, ULONG value)
{
    TRACE_NATIVE("SetClassLong(%u, %d, %X)", handle, index, value);
    Open32Window *window = open32FindWindow(handle);
    if (!window)
    {
        open32SetError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    if (index == WIN32_GCL_WNDPROC && !open32ValidProcedure(value))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (index == WIN32_GCL_MENUNAME && value > UINT16_MAX)
    {
        char *name = lx_execution_copy_string(GProcessState.execution, value, CLASS_NAME_LIMIT);
        if (!name)
        {
            open32SetError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        free(name);
    }
    Open32Class *c = window->class;
    lx_host_monitor_lock(monitor);
    ULONG previous = 0, *field = NULL;
    if (index >= 0)
    {
        if ((ULONG)index <= c->class_extra && c->class_extra - (ULONG)index >= 4)
        {
            previous = lx_guest_load_u32((uint8 *)c->class_data + index);
            lx_guest_store_u32((uint8 *)c->class_data + index, value);
        }
        else
            open32SetError(ERROR_INVALID_INDEX);
    }
    else if (index == WIN32_GCL_MENUNAME)
    {
        previous =
            c->menu ? lx_execution_mapping_address(c->names) + NAMES_MENU_OFFSET : c->menu_id;
        free(c->menu);
        c->menu = NULL;
        c->menu_id = value;
    }
    else
    {
        switch (index)
        {
            case WIN32_GCL_HBRBACKGROUND:
                field = &c->brush;
                break;
            case WIN32_GCL_HCURSOR:
                field = &c->cursor;
                break;
            case WIN32_GCL_HICON:
                field = &c->icon;
                break;
            case WIN32_GCL_HMODULE:
                field = &c->instance;
                break;
            case WIN32_GCL_WNDPROC:
                field = &c->procedure;
                break;
            case WIN32_GCL_STYLE:
                field = &c->style;
                break;
            case WIN32_GCL_HICONSM:
                field = &c->small_icon;
                break;
            case WIN32_GCL_CBWNDEXTRA:
            case WIN32_GCL_CBCLSEXTRA:
                open32SetError(ERROR_NOT_SUPPORTED);
                break;
            default:
                open32SetError(ERROR_INVALID_INDEX);
                break;
        }
        if (field)
        {
            previous = *field;
            *field = value;
        }
    }
    lx_host_monitor_unlock(monitor);
    return previous;
}

LONG GetClassName(ULONG handle, PCHAR output, LONG capacity)
{
    Open32Window *window = open32FindWindow(handle);
    const char *name = window                             ? window->class->name
                       : open32NativeDialogWindow(handle) ? open32NativeClassName(handle)
                                                          : NULL;
    if (!name || !output || capacity <= 0)
    {
        open32SetError(name ? ERROR_INVALID_PARAMETER : ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    size_t length = strlen(name);
    if (length >= (size_t)capacity)
        length = (size_t)capacity - 1;
    memcpy(output, name, length);
    output[length] = 0;
    return (LONG)length;
}
