/* Public domain. No rights reserved. Wholly AI-authored. */
/* Physical face identity and grayscale glyph coverage survive repeated font-state queries. */
#include "os2/include/pmwin.h"
#include "os2/include/pmgpi.h"
#include "os2/ps/pmmerge/pmwin/graphics.h"
#include "os2/ps/common/pm_text.h"
#include "host/font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static FATTRS attributes(const FONTMETRICS *metrics, USHORT selection)
{
    FATTRS result = {0};
    result.usRecordLength = sizeof result;
    result.fsSelection = selection;
    result.lMatch = metrics->lMatch;
    strcpy(result.szFacename, metrics->szFacename);
    result.usCodePage = metrics->usCodePage;
    result.lMaxBaselineExt = metrics->lMaxBaselineExt;
    result.lAveCharWidth = metrics->lAveCharWidth;
    result.fsFontUse = 2; /* FATTR_FONTUSE_OUTLINE */
    return result;
}

static int stable_glyph_masks(void)
{
    const unsigned sizes[] = {11, 16, 24};
    const uint32_t points[] = {'U', 'n', 't', 'i', 'l', 'e', 'd', '?', 0x2022};
    for (unsigned index = 0; index < lx_host_font_count(); index++)
        for (unsigned size = 0; size < sizeof sizes / sizeof sizes[0]; size++)
        {
            LxHostFont *font = lx_host_font_open(index, sizes[size]);
            CHECK(font);
            for (unsigned point = 0; point < sizeof points / sizeof points[0]; point++)
            {
                LxHostGlyph reference;
                CHECK(lx_host_font_glyph(font, points[point], &reference));
                for (unsigned iteration = 0; iteration < 4; iteration++)
                {
                    LxHostFontMetrics metrics;
                    CHECK(lx_host_font_metrics(font, &metrics));
                    CHECK(metrics.em == (int)sizes[size]);
                    CHECK(lx_host_font_advance(font, 'M') > 0);
                    (void)lx_host_font_kerning(font, 'A', 'V');
                    LxHostGlyph other, repeated;
                    CHECK(lx_host_font_glyph(font, 'W', &other));
                    lx_host_glyph_free(&other);
                    CHECK(lx_host_font_glyph(font, points[point], &repeated));
                    CHECK(repeated.width == reference.width &&
                          repeated.height == reference.height && repeated.left == reference.left &&
                          repeated.top == reference.top && repeated.advance == reference.advance);
                    size_t bytes = (size_t)reference.width * reference.height;
                    CHECK(!bytes || !memcmp(repeated.coverage, reference.coverage, bytes));
                    lx_host_glyph_free(&repeated);
                }
                lx_host_glyph_free(&reference);
            }
            lx_host_font_close(font);
        }
    return 0;
}

int main(void)
{
    CHECK(!stable_glyph_masks());
    HAB anchor = WinInitialize(0);
    CHECK(anchor);
    HMQ queue = WinCreateMsgQueue(anchor, 0);
    CHECK(queue);
    HPS ps = WinGetScreenPS(HWND_DESKTOP);
    CHECK(ps);
    FONTMETRICS original, selected;
    CHECK(GpiQueryFontMetrics(ps, sizeof original, &original));
    CHECK(original.lMatch == 1 && !strcmp(original.szFacename, "Helv") && !original.fsSelection);
    for (unsigned i = 0; i < 3; i++)
    {
        FATTRS bold = attributes(&original, 0x20);
        CHECK(GpiCreateLogFont(ps, NULL, 2, &bold) == 2 && GpiSetCharSet(ps, 2));
        LxHostFontMetrics rendered;
        CHECK(lx_host_font_metrics(lx_pm_font(ps), &rendered) && rendered.bold);
        CHECK(GpiQueryFontMetrics(ps, sizeof selected, &selected));
        CHECK(selected.lMatch == original.lMatch && !strcmp(selected.szFacename, "Helv") &&
              !selected.fsSelection && selected.lEmHeight == original.lEmHeight &&
              selected.lMaxBaselineExt == original.lMaxBaselineExt);
        FATTRS normal = attributes(&selected, selected.fsSelection);
        CHECK(GpiCreateLogFont(ps, NULL, 0, &normal) == 2 && GpiSetCharSet(ps, 0));
        CHECK(lx_host_font_metrics(lx_pm_font(ps), &rendered) && !rendered.bold);
        CHECK(GpiQueryFontMetrics(ps, sizeof selected, &selected) &&
              selected.lEmHeight == original.lEmHeight &&
              selected.lMaxBaselineExt == original.lMaxBaselineExt);
    }
    FATTRS explicit_bold = attributes(&original, 0x20);
    explicit_bold.lMatch = 2;
    strcpy(explicit_bold.szFacename, "Helv Bold");
    CHECK(GpiCreateLogFont(ps, NULL, 3, &explicit_bold) == 2 && GpiSetCharSet(ps, 3));
    CHECK(GpiQueryFontMetrics(ps, sizeof selected, &selected) && selected.lMatch == 2 &&
          (selected.fsSelection & 0x20) && !strcmp(selected.szFacename, "Helv Bold"));
    SIZEF box = {24 * 65536, 24 * 65536};
    CHECK(GpiSetCharBox(ps, &box) && GpiQueryFontMetrics(ps, sizeof selected, &selected) &&
          selected.lEmHeight == 24);
    CHECK(GpiSetCharSet(ps, 0) && GpiQueryFontMetrics(ps, sizeof selected, &selected) &&
          selected.lEmHeight == 24 && !selected.fsSelection);
    CHECK(lx_pm_codepoint(7) == 0x2022 && lx_pm_codepoint(0x7f) == 0x2302 &&
          lx_pm_codepoint('H') == 'H');
    LxHostGlyph glyph;
    CHECK(lx_host_font_glyph(lx_pm_font(ps), lx_pm_codepoint(7), &glyph));
    CHECK(glyph.width && glyph.height && glyph.advance > 0);
    lx_host_glyph_free(&glyph);
    CHECK(WinReleasePS(ps) && WinDestroyMsgQueue(queue) && WinTerminate(anchor));
    return 0;
}
