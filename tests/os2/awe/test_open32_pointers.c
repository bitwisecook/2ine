/* Public domain. No rights reserved. Wholly AI-authored. */
/* Injected PM queries exercise coordinates without initializing or moving a host cursor. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/include/pmwin.h"
#include "os2/include/private/pmp.h"
#include "emulator/abi.h"
#include "emulator/handles.h"
#include <stdio.h>

/* No confinement rectangle is active in these coordinate cases. */
void open32ClipCursorPoint(LONG *x, LONG *y);

void open32ClipCursorPoint(LONG *x, LONG *y)
{
    (void)x;
    (void)y;
}

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

extern BOOL OS2API GetCursorPos(PVOID output);
extern BOOL OS2API SetCursorPos(LONG x, LONG y);
LxProcessState GProcessState;
static ULONG error;
static ULONG presentation = 1;
static unsigned presentation_calls, query_calls, set_calls;
static BOOL set_result = TRUE;
static BOOL query_result = TRUE;
static POINTL position;
static LONG height = 1080;

uint64_t lx_host_thread_id(void)
{
    return 0;
}

ULONG open32Presentation(void)
{
    presentation_calls++;
    return presentation;
}

void open32SetError(ULONG value)
{
    error = value;
}

BOOL WinQueryPointerPos(HWND desktop, PPOINTL output)
{
    CHECK(desktop == HWND_DESKTOP);
    query_calls++;
    *output = position;
    return query_result;
}

LONG WinQuerySysValue(HWND desktop, LONG index)
{
    CHECK(desktop == HWND_DESKTOP && index == 21);
    return height;
}

BOOL WinSetPointerPos(HWND desktop, LONG x, LONG y)
{
    CHECK(desktop == HWND_DESKTOP);
    set_calls++;
    if (set_result)
        position = (POINTL){x, y};
    return set_result;
}

/* These entrypoints must never be reached by a position query. */
int lx_handle_borrow(uint32_t handle, LxHandleKind kind, LxHandleBorrow *borrow)
{
    (void)handle;
    (void)kind;
    (void)borrow;
    CHECK(0);
    return 0;
}

void lx_handle_release(LxHandleBorrow *borrow)
{
    (void)borrow;
    CHECK(0);
}

HPOINTER lx_pm_system_pointer(LONG id)
{
    (void)id;
    CHECK(0);
    return 0;
}

HPOINTER WinLoadPointer(HWND desktop, HMODULE module, ULONG id)
{
    (void)desktop;
    (void)module;
    (void)id;
    CHECK(0);
    return 0;
}

BOOL WinSetPointer(HWND desktop, HPOINTER pointer)
{
    (void)desktop;
    (void)pointer;
    CHECK(0);
    return FALSE;
}

BOOL WinDestroyPointer(HPOINTER pointer)
{
    (void)pointer;
    CHECK(0);
    return FALSE;
}

static void unchanged(const UCHAR *bytes)
{
    for (unsigned n = 0; n < 10; n++)
        CHECK(bytes[n] == 0xa5);
}

int main(void)
{
    UCHAR guarded[10];
    memset(guarded, 0xa5, sizeof guarded);
    CHECK(!GetCursorPos(NULL) && error == ERROR_INVALID_PARAMETER);
    CHECK(presentation_calls == 0 && query_calls == 0);
    presentation = 0;
    CHECK(!GetCursorPos(guarded + 1) && error == ERROR_NOT_ENOUGH_MEMORY);
    CHECK(presentation_calls == 1 && query_calls == 0);
    unchanged(guarded);
    CHECK(!SetCursorPos(0, 0) && error == ERROR_NOT_ENOUGH_MEMORY && set_calls == 0);
    presentation = 1;

    static const struct
    {
        LONG x, pm_y, screen_y;
    } cases[] = {{0, 1079, 0},      {0, 0, 1079},         {-1920, 1089, -10},
                 {2000, -21, 1100}, {INT32_MIN, 1079, 0}, {INT32_MAX, 0, 1079}};

    for (unsigned n = 0; n < sizeof cases / sizeof *cases; n++)
    {
        position = (POINTL){cases[n].x, cases[n].pm_y};
        error = 0x12345678;
        CHECK(GetCursorPos(guarded + 1));
        CHECK((LONG)lx_guest_load_u32(guarded + 1) == cases[n].x);
        CHECK((LONG)lx_guest_load_u32(guarded + 5) == cases[n].screen_y);
        CHECK(guarded[0] == 0xa5 && guarded[9] == 0xa5);
        CHECK(error == 0x12345678);
        CHECK(SetCursorPos(cases[n].x, cases[n].screen_y));
        CHECK(position.x == cases[n].x && position.y == cases[n].pm_y);
        CHECK(error == 0x12345678);
        CHECK(GetCursorPos(guarded + 1));
        CHECK((LONG)lx_guest_load_u32(guarded + 1) == cases[n].x);
        CHECK((LONG)lx_guest_load_u32(guarded + 5) == cases[n].screen_y);
    }
    height = 1;
    position = (POINTL){-1, 0};
    CHECK(GetCursorPos(guarded + 1));
    CHECK(lx_guest_load_u32(guarded + 5) == 0);
    memset(guarded, 0xa5, sizeof guarded);
    query_result = FALSE;
    CHECK(!GetCursorPos(guarded + 1) && error == ERROR_GEN_FAILURE);
    unchanged(guarded);
    query_result = TRUE;
    height = 0;
    CHECK(!GetCursorPos(guarded + 1) && error == ERROR_GEN_FAILURE);
    unchanged(guarded);
    height = 1080;
    position.y = INT32_MIN;
    CHECK(!GetCursorPos(guarded + 1) && error == ERROR_INVALID_PARAMETER);
    unchanged(guarded);
    puts(
        "Open32 cursor positions preserve pixel boundaries, signed coordinates and failed outputs");
    return 0;
}
