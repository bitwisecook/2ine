/* Public domain. No rights reserved. Wholly AI-authored. */
/* Scan conversion of polygons and lines, shared by GPI and Open32 drawing. */
#include "os2/include/private/export.h"
#include "os2/include/private/pmgrep.h"
#include <math.h>

typedef struct ScanCrossing
{
    double x;
    int winding;
} ScanCrossing;

static int crossing_order(const void *a, const void *b)
{
    double x = ((const ScanCrossing *)a)->x, y = ((const ScanCrossing *)b)->x;
    return x < y ? -1 : x > y;
}

/* Collects the edges crossing the horizontal line at sample; an edge holds its lower end and
   excludes its upper end, so a vertex on the line is counted once. */
static size_t row_crossings(const PmScanPoint *points, const size_t *counts, size_t contours,
                            double sample, ScanCrossing *crossings)
{
    size_t n = 0, offset = 0;
    for (size_t c = 0; c < contours; offset += counts[c++])
        for (size_t i = 0; i < counts[c]; i++)
        {
            PmScanPoint a = points[offset + i], b = points[offset + (i + 1) % counts[c]];
            if (a.y == b.y || sample < (a.y < b.y ? a.y : b.y) || sample >= (a.y > b.y ? a.y : b.y))
                continue;
            crossings[n++] = (ScanCrossing){a.x + (sample - a.y) * (b.x - a.x) / (b.y - a.y),
                                            b.y > a.y ? 1 : -1};
        }
    return n;
}

BOOL lx_pm_scan_polygons(const PmScanPoint *points, const size_t *counts, size_t contours,
                         unsigned rule, LONG first_row, LONG end_row, LxPmSpanSink sink,
                         void *context)
{
    size_t total = 0;
    double low = INFINITY, high = -INFINITY;
    for (size_t c = 0; c < contours; c++)
        total += counts[c];
    for (size_t i = 0; i < total; i++)
    {
        low = fmin(low, points[i].y);
        high = fmax(high, points[i].y);
    }
    if (!total)
        return TRUE;
    BOOL winding_rule = (rule & LX_PM_SCAN_WINDING) != 0;
    BOOL inclusive = (rule & LX_PM_SCAN_INCLUSIVE) != 0;
    ScanCrossing *crossings = malloc(total * sizeof *crossings);
    if (!crossings)
        return FALSE;
    /* Rows whose centre lies in [low, high); an inclusive fill adds the row at high, sampled
       half a pel below it. */
    int64_t first = (int64_t)ceil(low - 0.5), last = (int64_t)ceil(high - 0.5);
    int64_t end = last + (inclusive ? 1 : 0);
    if (first < first_row)
        first = first_row;
    if (end > end_row)
        end = end_row;
    BOOL ok = TRUE;
    for (int64_t y = first; ok && y < end; y++)
    {
        double sample = y < last ? y + 0.5 : y - 0.5;
        size_t n = row_crossings(points, counts, contours, sample, crossings);
        qsort(crossings, n, sizeof *crossings, crossing_order);
        int winding = 0;
        double left = 0;
        for (size_t j = 0; ok && j < n; j++)
        {
            BOOL before = winding_rule ? winding != 0 : (winding & 1) != 0;
            winding += winding_rule ? crossings[j].winding : 1;
            BOOL after = winding_rule ? winding != 0 : (winding & 1) != 0;
            if (!before && after)
                left = crossings[j].x;
            if (before && !after)
            {
                double right = crossings[j].x;
                if (left < INT32_MIN || right >= INT32_MAX)
                {
                    ok = FALSE;
                    break;
                }
                LONG l = (LONG)ceil(left - 0.5), r = (LONG)ceil(right - 0.5) + (inclusive ? 1 : 0);
                if (r > l)
                    ok = sink(context, (LONG)y, l, r);
            }
        }
    }
    free(crossings);
    return ok;
}

BOOL lx_pm_walk_line(LONG x0, LONG y0, LONG x1, LONG y1, BOOL include_end, LxPmPelVisitor visit,
                     void *context)
{
    int64_t dx = llabs((int64_t)x1 - x0), dy = llabs((int64_t)y1 - y0), error = dx - dy;
    LONG x = x0, y = y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    for (;;)
    {
        BOOL at_end = x == x1 && y == y1;
        if (at_end && !include_end)
            return TRUE;
        if (!visit(context, x, y))
            return FALSE;
        if (at_end)
            return TRUE;
        int64_t twice = error * 2;
        if (twice > -dy)
        {
            error -= dy;
            x += sx;
        }
        if (twice < dx)
        {
            error += dx;
            y += sy;
        }
    }
}
