/* Public domain. No rights reserved. Wholly AI-authored. */
/* AdjustWindowRectEx sizing against distinct injected PM system values. */
#include "os2libs/os2native.h"
#include "os2libs/pmwin.h"
#include "os2libs/pmwinx.h"
#include <stdio.h>
#include <string.h>

static int failures;

LONG WinQuerySysValue(HWND desktop, LONG index)
{
    if (desktop != HWND_DESKTOP)
        failures++;
    switch (index)
    {
        case 4:
            return 3;
        case 5:
            return 5;
        case 26:
            return 7;
        case 27:
            return 11;
        case 28:
            return 13;
        case 29:
            return 17;
        case 30:
            return 19;
        case 35:
            return 23;
        default:
            failures++;
            return 0;
    }
}

static void check(const char *name, ULONG style, BOOL menu, ULONG extended, LONG left, LONG top,
                  LONG right, LONG bottom)
{
    /* One byte of padding leaves the rectangle unaligned, as a guest may. */
    unsigned char storage[1 + 4 * sizeof(LONG)];
    LONG edges[4] = {100, 200, 300, 400}, expected[4] = {left, top, right, bottom};
    memcpy(storage + 1, edges, sizeof edges);
    BOOL result = AdjustWindowRectEx(storage + 1, style, menu, extended);
    memcpy(edges, storage + 1, sizeof edges);
    if (result != TRUE || memcmp(edges, expected, sizeof edges))
    {
        printf("%s: result %d, rectangle %d %d %d %d\n", name, (int)result, (int)edges[0],
               (int)edges[1], (int)edges[2], (int)edges[3]);
        failures++;
    }
}

int main(void)
{
    check("overlapped", 0, FALSE, 0, 97, 176, 303, 405);
    check("overlapped window", 0x00CF0000, FALSE, 0, 97, 176, 303, 405);
    check("extra style bits", 0x10CF0000, FALSE, 0, 97, 176, 303, 405);
    check("menu", 0, TRUE, 0, 97, 141, 303, 405);
    check("extended style ignored", 0, FALSE, 0xFFFFFFFF, 97, 176, 303, 405);
    check("border", 0x00800000, FALSE, 0, 93, 189, 307, 411);
    check("dialog frame", 0x00400000, FALSE, 0, 87, 183, 313, 417);
    check("caption", 0x00C00000, FALSE, 0, 87, 164, 313, 417);
    check("thick frame", 0x00040000, FALSE, 0, 97, 195, 303, 405);
    check("system menu", 0x00080000, FALSE, 0, 100, 181, 300, 400);
    check("minimize box", 0x00020000, FALSE, 0, 100, 181, 300, 400);
    check("maximize box", 0x00010000, FALSE, 0, 100, 181, 300, 400);
    check("no frame", 0x40000000, FALSE, 0, 100, 200, 300, 400);
    check("thick over dialog", 0x00440000, FALSE, 0, 97, 195, 303, 405);
    if (failures)
        return 1;
    puts("AdjustWindowRectEx matches the original frame, title and menu rules");
    return 0;
}
