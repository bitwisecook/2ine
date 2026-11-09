/* Public domain. No rights reserved. Wholly AI-authored. */
/* Sound bank files the MIDI synthesizer loads: SoundFont 2/3 (RIFF "sfbk"),
   Downloadable Sounds (RIFF "DLS ") and SFZ text instruments. */
#include "formats/audio/soundbank.h"
#include "formats/endian.h"
#include <ctype.h>
#include <string.h>

#define RIFF_SIZE 4
#define RIFF_FORM 8
/* The chunk size counts the form type, so it is at least four. */
#define RIFF_FORM_BYTES 4u
#define RIFF_PREAMBLE_BYTES 8u

static int extension_equal(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b))
        a++, b++;
    return !*a && !*b;
}

const char *tb_soundbank_format(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return NULL;
    static const char *const formats[] = {"sf2", "sf3", "sfz", "dls", NULL};
    for (int i = 0; formats[i]; i++)
        if (extension_equal(dot + 1, formats[i]))
            return formats[i];
    return NULL;
}

int tb_soundbank_header_valid(const char *format, const unsigned char *header, uint64_t file_size)
{
    if (!strcmp(format, "sfz"))
        return 1;
    uint32_t chunk = tb_le32(header + RIFF_SIZE);
    return !memcmp(header, "RIFF", 4) &&
           !memcmp(header + RIFF_FORM, !strcmp(format, "dls") ? "DLS " : "sfbk", 4) &&
           chunk >= RIFF_FORM_BYTES && (uint64_t)chunk + RIFF_PREAMBLE_BYTES <= file_size;
}
