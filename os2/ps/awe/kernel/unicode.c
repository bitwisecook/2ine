/* Public domain. No rights reserved. Wholly AI-authored. */
/* Counted Open32 conversion uses UTF-16 wire units, never host wchar_t. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "host/codepage_tables.h"
#include <limits.h>

enum
{
    /* Largest conversion, in bytes on either side. */
    CONVERSION_WIRE_LIMIT = 16 * 1024 * 1024,
    UTF16_UNIT_BYTES = 2,
    /* Rows of lx_host_sbcs_unicode, and UTF-8, which has no row. */
    PAGE_CP437 = 0,
    PAGE_CP850 = 1,
    PAGE_CP1252 = 2,
    PAGE_UTF8 = 3,
    /* Code points 0-127 are ASCII in every page; the tables give 128-255. */
    SBCS_TABLE_FIRST = 128,
    SBCS_TABLE_SIZE = 128,
    /* The original refuses a default character longer than this many bytes. */
    DEFAULT_TEXT_LIMIT = 13,
    MB_VALID_FLAGS = WIN32_MB_PRECOMPOSED | WIN32_MB_COMPOSITE | WIN32_MB_USEGLYPHCHARS |
                     WIN32_MB_ERR_INVALID_CHARS,
    WC_COMPOSITE_FLAGS = WIN32_WC_DISCARDNS | WIN32_WC_SEPCHARS | WIN32_WC_DEFAULTCHAR,
    WC_VALID_FLAGS = WIN32_WC_COMPOSITECHECK | WC_COMPOSITE_FLAGS,
    REPLACEMENT_CHARACTER = 0xfffd,
    HIGH_SURROGATE_FIRST = 0xd800,
    HIGH_SURROGATE_LAST = 0xdbff,
    LOW_SURROGATE_FIRST = 0xdc00,
    LOW_SURROGATE_LAST = 0xdfff,
    SUPPLEMENTARY_FIRST = 0x10000,
    SURROGATE_BITS = 10,
    SURROGATE_LOW_MASK = 1023
};

static LONG conversion_error(ULONG error)
{
    open32SetError(error);
    return 0;
}

static int conversion_page(ULONG page)
{
    if (page == WIN32_CP_ACP)
        page = GetACP();
    else if (page == WIN32_CP_OEMCP)
        page = GetOEMCP();
    /* Code page 1004 is OS/2's name for the Windows Latin-1 page. */
    switch (page)
    {
        case 437:
            return PAGE_CP437;
        case 850:
            return PAGE_CP850;
        case 1252:
        case 1004:
            return PAGE_CP1252;
        case WIN32_CP_UTF8:
            return PAGE_UTF8;
        default:
            return -1;
    }
}

static int overlaps(const void *input, size_t input_bytes, const void *output, size_t output_bytes)
{
    uintptr_t first = (uintptr_t)input, second = (uintptr_t)output;
    return input_bytes && output_bytes &&
           (first <= second ? second - first < input_bytes : first - second < output_bytes);
}

/* Advance one Unicode maximal subpart. Invalid input is marked, not silently
   accepted as a scalar. A non-strict caller can emit U+FFFD for that subpart. */
static uint32 utf8_point(const uint8 *input, size_t count, size_t *offset, BOOL *invalid)
{
    size_t at = *offset;
    uint32 first = input[at++], value;
    unsigned remaining;
    if (first < 128)
    {
        *offset = at;
        return first;
    }
    if (first >= 0xc2 && first <= 0xdf)
        value = first & 31, remaining = 1;
    else if (first >= 0xe0 && first <= 0xef)
        value = first & 15, remaining = 2;
    else if (first >= 0xf0 && first <= 0xf4)
        value = first & 7, remaining = 3;
    else
        goto bad;
    for (unsigned i = 0; i < remaining; i++)
    {
        if (at >= count)
            goto bad;
        uint8 next = input[at];
        if (next < 0x80 || next > 0xbf ||
            (!i && ((first == 0xe0 && next < 0xa0) || (first == 0xed && next > 0x9f) ||
                    (first == 0xf0 && next < 0x90) || (first == 0xf4 && next > 0x8f))))
            goto bad;
        value = (value << 6) | (next & 63);
        at++;
    }
    *offset = at;
    return value;
bad:
    *offset = at;
    *invalid = TRUE;
    return REPLACEMENT_CHARACTER;
}

static uint32 utf16_point(const uint8 *input, size_t units, size_t *offset, BOOL *invalid)
{
    uint32 value = lx_guest_load_u16(input + UTF16_UNIT_BYTES * (*offset)++);
    if (value >= HIGH_SURROGATE_FIRST && value <= HIGH_SURROGATE_LAST)
    {
        if (*offset < units)
        {
            uint32 second = lx_guest_load_u16(input + UTF16_UNIT_BYTES * *offset);
            if (second >= LOW_SURROGATE_FIRST && second <= LOW_SURROGATE_LAST)
            {
                (*offset)++;
                return SUPPLEMENTARY_FIRST + ((value - HIGH_SURROGATE_FIRST) << SURROGATE_BITS) +
                       second - LOW_SURROGATE_FIRST;
            }
        }
        *invalid = TRUE;
        return REPLACEMENT_CHARACTER;
    }
    if (value >= LOW_SURROGATE_FIRST && value <= LOW_SURROGATE_LAST)
    {
        *invalid = TRUE;
        return REPLACEMENT_CHARACTER;
    }
    return value;
}

static unsigned utf8_bytes(uint32 point, uint8 bytes[4])
{
    if (point < 0x80)
    {
        bytes[0] = (uint8)point;
        return 1;
    }
    if (point < 0x800)
    {
        bytes[0] = 0xc0 | (point >> 6);
        bytes[1] = 0x80 | (point & 63);
        return 2;
    }
    if (point < 0x10000)
    {
        bytes[0] = 0xe0 | (point >> 12);
        bytes[1] = 0x80 | ((point >> 6) & 63);
        bytes[2] = 0x80 | (point & 63);
        return 3;
    }
    bytes[0] = 0xf0 | (point >> 18);
    bytes[1] = 0x80 | ((point >> 12) & 63);
    bytes[2] = 0x80 | ((point >> 6) & 63);
    bytes[3] = 0x80 | (point & 63);
    return 4;
}

LONG MultiByteToWideChar(ULONG codepage, ULONG flags, PSZ input, LONG count, PVOID output,
                         LONG capacity)
{
    TRACE_NATIVE("MultiByteToWideChar(%u, %X, %p, %d, %p, %d)", codepage, flags, input, count,
                 output, capacity);
    if (!input || !count || count < -1 || capacity < 0 || (capacity && !output) ||
        (capacity && (void *)input == output))
        return conversion_error(ERROR_INVALID_PARAMETER);
    /* MB_PRECOMPOSED and MB_COMPOSITE exclude each other. */
    if ((flags & ~(unsigned)MB_VALID_FLAGS) ||
        (flags & (unsigned)(WIN32_MB_PRECOMPOSED | WIN32_MB_COMPOSITE)) ==
            (unsigned)(WIN32_MB_PRECOMPOSED | WIN32_MB_COMPOSITE))
        return conversion_error(ERROR_INVALID_FLAGS);
    int page = conversion_page(codepage);
    if (page < 0 || (flags & (WIN32_MB_COMPOSITE | WIN32_MB_USEGLYPHCHARS)) ||
        (page == PAGE_UTF8 && (flags & WIN32_MB_PRECOMPOSED)))
        return conversion_error(ERROR_NOT_SUPPORTED);
    size_t bytes = 0;
    if (count == -1)
    {
        while (bytes < CONVERSION_WIRE_LIMIT && input[bytes])
            bytes++;
        if (bytes == CONVERSION_WIRE_LIMIT)
            return conversion_error(ERROR_NOT_ENOUGH_MEMORY);
        bytes++;
    }
    else
        bytes = (size_t)count;
    if (bytes > CONVERSION_WIRE_LIMIT ||
        (size_t)capacity > CONVERSION_WIRE_LIMIT / UTF16_UNIT_BYTES)
        return conversion_error(ERROR_NOT_ENOUGH_MEMORY);
    if (capacity && overlaps(input, bytes, output, (size_t)capacity * UTF16_UNIT_BYTES))
        return conversion_error(ERROR_INVALID_PARAMETER);
    size_t required = 0;
    for (size_t at = 0; at < bytes;)
    {
        BOOL invalid = FALSE;
        uint32 point = page == PAGE_UTF8 ? utf8_point((uint8 *)input, bytes, &at, &invalid)
                                         : (uint8)input[at++];
        if (invalid && (flags & WIN32_MB_ERR_INVALID_CHARS))
            return conversion_error(ERROR_NO_UNICODE_TRANSLATION);
        required += point >= SUPPLEMENTARY_FIRST ? 2 : 1;
    }
    if (required > CONVERSION_WIRE_LIMIT / UTF16_UNIT_BYTES)
        return conversion_error(ERROR_NOT_ENOUGH_MEMORY);
    if (!capacity)
        return (LONG)required;
    if ((size_t)capacity < required)
        return conversion_error(ERROR_INSUFFICIENT_BUFFER);
    size_t written = 0;
    for (size_t at = 0; at < bytes;)
    {
        BOOL invalid = FALSE;
        uint32 point;
        if (page == PAGE_UTF8)
            point = utf8_point((uint8 *)input, bytes, &at, &invalid);
        else
        {
            uint8 value = (uint8)input[at++];
            point = value < SBCS_TABLE_FIRST ? value
                                             : lx_host_sbcs_unicode[page][value - SBCS_TABLE_FIRST];
        }
        size_t needed = point >= SUPPLEMENTARY_FIRST ? 2 : 1;
        if (written > (size_t)capacity || needed > (size_t)capacity - written)
            return conversion_error(ERROR_INSUFFICIENT_BUFFER);
        if (point >= SUPPLEMENTARY_FIRST)
        {
            lx_guest_store_u16((uint8 *)output + UTF16_UNIT_BYTES * written++,
                               HIGH_SURROGATE_FIRST +
                                   ((point - SUPPLEMENTARY_FIRST) >> SURROGATE_BITS));
            point = LOW_SURROGATE_FIRST + ((point - SUPPLEMENTARY_FIRST) & SURROGATE_LOW_MASK);
        }
        lx_guest_store_u16((uint8 *)output + UTF16_UNIT_BYTES * written++, (uint16)point);
    }
    return (LONG)written;
}

LONG WideCharToMultiByte(ULONG codepage, ULONG flags, PVOID input, LONG count, PSZ output,
                         LONG capacity, PSZ default_text, PULONG used_default)
{
    TRACE_NATIVE("WideCharToMultiByte(%u, %X, %p, %d, %p, %d, %p, %p)", codepage, flags, input,
                 count, output, capacity, default_text, used_default);
    if (!input || !count || count < -1 || (capacity && output == input))
        return conversion_error(ERROR_INVALID_PARAMETER);
    /* The composite-handling flags need WC_COMPOSITECHECK and exclude each other. */
    if ((flags & ~(unsigned)WC_VALID_FLAGS) ||
        ((flags & WIN32_WC_DISCARDNS) && (flags & WIN32_WC_DEFAULTCHAR)) ||
        (!(flags & WIN32_WC_COMPOSITECHECK) && (flags & WC_COMPOSITE_FLAGS)))
        return conversion_error(ERROR_INVALID_FLAGS);
    size_t replacement_bytes = 1;
    const uint8 *replacement = (const uint8 *)"?";
    if (default_text)
    {
        replacement_bytes = 0;
        while (replacement_bytes < DEFAULT_TEXT_LIMIT + 1 && default_text[replacement_bytes])
            replacement_bytes++;
        if (!replacement_bytes || replacement_bytes > DEFAULT_TEXT_LIMIT)
            return conversion_error(ERROR_INTERNAL_ERROR);
        replacement = (const uint8 *)default_text;
    }
    if (capacity < 0)
        return conversion_error(ERROR_INSUFFICIENT_BUFFER);
    if (capacity && !output)
        return conversion_error(ERROR_INVALID_PARAMETER);
    int page = conversion_page(codepage);
    if (page < 0 || flags || (page == PAGE_UTF8 && default_text))
        return conversion_error(ERROR_NOT_SUPPORTED);
    size_t units = 0;
    if (count == -1)
    {
        while (units < CONVERSION_WIRE_LIMIT / UTF16_UNIT_BYTES &&
               lx_guest_load_u16((uint8 *)input + UTF16_UNIT_BYTES * units))
            units++;
        if (units == CONVERSION_WIRE_LIMIT / UTF16_UNIT_BYTES)
            return conversion_error(ERROR_NOT_ENOUGH_MEMORY);
        units++;
    }
    else
        units = (size_t)count;
    if (units > CONVERSION_WIRE_LIMIT / UTF16_UNIT_BYTES ||
        (size_t)capacity > CONVERSION_WIRE_LIMIT)
        return conversion_error(ERROR_NOT_ENOUGH_MEMORY);
    if (capacity && overlaps(input, units * UTF16_UNIT_BYTES, output, (size_t)capacity))
        return conversion_error(ERROR_INVALID_PARAMETER);
    size_t required = 0;
    BOOL used = FALSE;
    for (unsigned pass = 0; pass < (capacity ? 2u : 1u); pass++)
    {
        size_t written = 0;
        for (size_t at = 0; at < units;)
        {
            BOOL invalid = FALSE;
            uint32 point = utf16_point(input, units, &at, &invalid);
            uint8 bytes[4];
            const uint8 *selected = bytes;
            size_t length = 1;
            if (page == PAGE_UTF8)
            {
                length = utf8_bytes(point, bytes);
                used |= invalid;
            }
            else if (point < SBCS_TABLE_FIRST)
                bytes[0] = (uint8)point;
            else
            {
                unsigned found;
                for (found = 0; found < SBCS_TABLE_SIZE; found++)
                    if (lx_host_sbcs_unicode[page][found] == point)
                        break;
                if (found < SBCS_TABLE_SIZE)
                    bytes[0] = (uint8)(found + SBCS_TABLE_FIRST);
                else
                {
                    selected = replacement;
                    length = replacement_bytes;
                    used = TRUE;
                }
            }
            if (written > CONVERSION_WIRE_LIMIT - length)
                return conversion_error(ERROR_NOT_ENOUGH_MEMORY);
            if (pass)
            {
                if (written > (size_t)capacity || length > (size_t)capacity - written)
                    return conversion_error(ERROR_INSUFFICIENT_BUFFER);
                memcpy(output + written, selected, length);
            }
            written += length;
        }
        if (!pass)
        {
            required = written;
            if (capacity && (size_t)capacity < required)
                return conversion_error(ERROR_INSUFFICIENT_BUFFER);
        }
    }
    if (used_default)
        lx_guest_store_u32(used_default, used);
    return (LONG)required;
}
