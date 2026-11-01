/* Public domain. No rights reserved. Wholly AI-authored. */
/* Guest resource names are fixed-width integers, never truncated host pointers. */
#ifndef TWOINE_GUEST_HANDLES_H
#define TWOINE_GUEST_HANDLES_H
#include <stdint.h>
#include <stddef.h>

typedef enum LxHandleKind
{
    LX_HANDLE_MODULE = 1,
    LX_HANDLE_THREAD,
    LX_HANDLE_EVENT,
    LX_HANDLE_MUTEX,
    LX_HANDLE_DIRECTORY,
    LX_HANDLE_FILE,
    LX_HANDLE_PROFILE,
    LX_HANDLE_REGISTRY_KEY,
    LX_HANDLE_PM_ENUM,
    LX_HANDLE_PM_DC,
    LX_HANDLE_PM_BITMAP,
    LX_HANDLE_PM_ACCEL,
    LX_HANDLE_PM_REGION,
    LX_HANDLE_PM_POINTER,
    LX_HANDLE_GL_CONTEXT,
    LX_HANDLE_QUEUE,
    LX_HANDLE_SWITCH,
    LX_HANDLE_PM_DRAG_STRING,
    LX_HANDLE_OPEN32_FONT,
    LX_HANDLE_OPEN32_ICON,
    LX_HANDLE_OPEN32_THREAD,
    LX_HANDLE_OPEN32_BRUSH,
    LX_HANDLE_OPEN32_PALETTE,
    LX_HANDLE_OPEN32_REGION,
    LX_HANDLE_OPEN32_HOOK,
    LX_HANDLE_OPEN32_FIND,
    LX_HANDLE_OPEN32_MEMORY,
    LX_HANDLE_OPEN32_PEN,
    LX_HANDLE_KIND_LIMIT
} LxHandleKind;
typedef struct LxHandleBorrow
{
    void *resource;
    void *lease;
} LxHandleBorrow;
/* Repeated inserts of one resource share ownership. The kind and destructor must match.
   A closed handle is rejected immediately; active borrows keep its resource
   alive until release. Slot generations are retired before they can wrap. */
uint32_t lx_handle_create(LxHandleKind kind, void *resource, void (*destroy)(void *));
int lx_handle_borrow(uint32_t handle, LxHandleKind kind, LxHandleBorrow *borrow);
void lx_handle_release(LxHandleBorrow *borrow);
int lx_handle_close(uint32_t handle, LxHandleKind kind);
size_t lx_handle_count(LxHandleKind kind, const void *resource);
/* Guard runs under the registry lock and must not call registry functions.
   Returns 1 on close, 0 for an invalid handle, -1 when busy. */
int lx_handle_close_guarded(uint32_t handle, LxHandleKind kind, size_t allowed_borrows,
                            int (*guard)(void *));
#endif
