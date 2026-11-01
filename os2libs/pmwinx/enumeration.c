/* Public domain. No rights reserved. Wholly AI-authored. */
/* Child enumeration snapshots the descendant tree before guest callbacks run. */
#include "os2libs/pmwinx/windows.h"
#include "os2libs/pmwinx/internal.h"
typedef struct WindowSnapshot
{
    ULONG *handles;
    size_t count, capacity;
} WindowSnapshot;
static BOOL collect(WindowSnapshot *snapshot, ULONG parent, unsigned depth)
{
    if (depth > 256)
        return FALSE;
    for (ULONG child = WinQueryWindow(parent, 2); child; child = WinQueryWindow(child, 0))
    {
        if (open32FindWindow(child))
        {
            if (snapshot->count == snapshot->capacity)
            {
                size_t capacity = snapshot->capacity ? snapshot->capacity * 2 : 32;
                if (capacity > 65536)
                    return FALSE;
                ULONG *next = realloc(snapshot->handles, capacity * sizeof *next);
                if (!next)
                    return FALSE;
                snapshot->handles = next;
                snapshot->capacity = capacity;
            }
            snapshot->handles[snapshot->count++] = child;
        }
        if (!collect(snapshot, child, depth + 1))
            return FALSE;
    }
    return TRUE;
}
BOOL EnumChildWindows(ULONG parent, ULONG procedure, ULONG parameter)
{
    TRACE_NATIVE("EnumChildWindows(%u, %X, %X)", parent, procedure, parameter);
    if (!parent)
        parent = HWND_DESKTOP;
    if (!open32Presentation() || !WinIsWindow(open32Presentation(), parent) ||
        !open32ValidProcedure(procedure))
    {
        open32SetError(87);
        return FALSE;
    }
    WindowSnapshot snapshot = {0};
    if (!collect(&snapshot, parent, 0))
    {
        free(snapshot.handles);
        open32SetError(8);
        return FALSE;
    }
    BOOL result = TRUE;
    for (size_t i = 0; i < snapshot.count; i++)
    {
        ULONG child = snapshot.handles[i];
        if (!open32FindWindow(child))
            continue;
        ULONG ancestor = WinQueryWindow(child, 5);
        while (ancestor && ancestor != parent)
            ancestor = WinQueryWindow(ancestor, 5);
        if (!ancestor)
            continue;
        if (!lx_execution_callback32_4(GLoaderState.execution, procedure, child, parameter, 0, 0))
        {
            result = FALSE;
            break;
        }
    }
    free(snapshot.handles);
    return result;
}
