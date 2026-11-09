/* Public domain. No rights reserved. Wholly AI-authored. */
/* Win32 RECT arithmetic through PMWINX: half-open edges, emptiness and Windows' empty results. */
#include "os2/include/private/export.h"
#include "os2/include/os2win.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static BOOL equals(LONG *rect, LONG left, LONG top, LONG right, LONG bottom)
{
    return rect[0] == left && rect[1] == top && rect[2] == right && rect[3] == bottom;
}

int main(void)
{
    LONG a[4], b[4], out[4];
    CHECK(SetRect(a, 0, 0, 10, 10) && SetRect(b, 5, 5, 15, 15));
    CHECK(IntersectRect(out, a, b) && equals(out, 5, 5, 10, 10));
    CHECK(UnionRect(out, a, b) && equals(out, 0, 0, 15, 15));
    /* Touching rectangles do not intersect, and the result is cleared. */
    CHECK(SetRect(b, 10, 0, 20, 10) && !IntersectRect(out, a, b) && equals(out, 0, 0, 0, 0));
    /* Two empty rectangles unite to an all-zero rectangle, not to the second one. */
    CHECK(SetRect(a, 3, 3, 3, 9) && SetRect(b, 7, 7, 4, 4));
    memset(out, 0x55, sizeof out);
    CHECK(!UnionRect(out, a, b) && equals(out, 0, 0, 0, 0));
    CHECK(UnionRect(out, a, (LONG[4]){1, 2, 3, 4}) && equals(out, 1, 2, 3, 4));
    CHECK(IsRectEmpty(a) && !IsRectEmpty((LONG[4]){0, 0, 1, 1}));
    CHECK(SetRect(a, 0, 0, 10, 10) && SetRect(b, 0, 0, 10, 4));
    CHECK(SubtractRect(out, a, b) && equals(out, 0, 4, 10, 10));
    CHECK(SetRect(b, 2, 2, 4, 4) && SubtractRect(out, a, b) && equals(out, 0, 0, 10, 10));
    CHECK(OffsetRect(a, -3, 2) && equals(a, -3, 2, 7, 12));
    CHECK(InflateRect(a, 1, -1) && equals(a, -4, 3, 8, 11));
    CHECK(PtInRect(a, -4, 3) && !PtInRect(a, 8, 5) && !PtInRect(a, 0, 11));
    CHECK(CopyRect(out, a) && EqualRect(out, a) && !EqualRect(out, b));
    CHECK(SetRectEmpty(out) && equals(out, 0, 0, 0, 0));
    puts("Open32 rectangle arithmetic passed");
    return 0;
}
