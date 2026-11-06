/* Public domain. No rights reserved. Wholly AI-authored. */
/* Counted Open32 conversion uses UTF-16 wire units, never host wchar_t. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include "hal/host_codepages.h"
#include <limits.h>

enum
{
    O32_MB_PRECOMPOSED = 1,
    O32_MB_COMPOSITE = 2,
    O32_MB_USEGLYPHCHARS = 4,
    O32_MB_ERR_INVALID_CHARS = 8,
    O32_WC_DISCARDNS = 0x10,
    O32_WC_SEPCHARS = 0x20,
    O32_WC_DEFAULTCHAR = 0x40,
    O32_WC_COMPOSITECHECK = 0x200,
    O32_ERROR_NOT_ENOUGH_MEMORY = 8,
    O32_ERROR_NOT_SUPPORTED = 50,
    O32_ERROR_INVALID_PARAMETER = 87,
    O32_ERROR_INSUFFICIENT_BUFFER = 122,
    O32_ERROR_INVALID_FLAGS = 1004,
    O32_ERROR_NO_UNICODE_TRANSLATION = 1113,
    O32_ERROR_INTERNAL_ERROR = 1359,
    O32_CONVERSION_WIRE_LIMIT = 16 * 1024 * 1024
};

static LONG conversion_error(ULONG error)
{
    open32SetError(error);
    return 0;
}

static int conversion_page(ULONG page)
{
    if (page == 0)
        page = GetACP();
    else if (page == 1)
        page = GetOEMCP();
    switch (page)
    {
        case 437:
            return 0;
        case 850:
            return 1;
        case 1252:
        case 1004:
            return 2;
        case 65001:
            return 3;
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
    return 0xfffd;
}

static uint32 utf16_point(const uint8 *input, size_t units, size_t *offset, BOOL *invalid)
{
    uint32 value = lx_guest_load_u16(input + 2 * (*offset)++);
    if (value >= 0xd800 && value <= 0xdbff)
    {
        if (*offset < units)
        {
            uint32 second = lx_guest_load_u16(input + 2 * *offset);
            if (second >= 0xdc00 && second <= 0xdfff)
            {
                (*offset)++;
                return 0x10000 + ((value - 0xd800) << 10) + second - 0xdc00;
            }
        }
        *invalid = TRUE;
        return 0xfffd;
    }
    if (value >= 0xdc00 && value <= 0xdfff)
    {
        *invalid = TRUE;
        return 0xfffd;
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
        return conversion_error(O32_ERROR_INVALID_PARAMETER);
    if ((flags & ~15u) || (flags & 3u) == 3u)
        return conversion_error(O32_ERROR_INVALID_FLAGS);
    int page = conversion_page(codepage);
    if (page < 0 || (flags & (O32_MB_COMPOSITE | O32_MB_USEGLYPHCHARS)) ||
        (page == 3 && (flags & O32_MB_PRECOMPOSED)))
        return conversion_error(O32_ERROR_NOT_SUPPORTED);
    size_t bytes = 0;
    if (count == -1)
    {
        while (bytes < O32_CONVERSION_WIRE_LIMIT && input[bytes])
            bytes++;
        if (bytes == O32_CONVERSION_WIRE_LIMIT)
            return conversion_error(O32_ERROR_NOT_ENOUGH_MEMORY);
        bytes++;
    }
    else
        bytes = (size_t)count;
    if (bytes > O32_CONVERSION_WIRE_LIMIT || (size_t)capacity > O32_CONVERSION_WIRE_LIMIT / 2)
        return conversion_error(O32_ERROR_NOT_ENOUGH_MEMORY);
    if (capacity && overlaps(input, bytes, output, (size_t)capacity * 2))
        return conversion_error(O32_ERROR_INVALID_PARAMETER);
    size_t required = 0;
    for (size_t at = 0; at < bytes;)
    {
        BOOL invalid = FALSE;
        uint32 point =
            page == 3 ? utf8_point((uint8 *)input, bytes, &at, &invalid) : (uint8)input[at++];
        if (invalid && (flags & O32_MB_ERR_INVALID_CHARS))
            return conversion_error(O32_ERROR_NO_UNICODE_TRANSLATION);
        required += point >= 0x10000 ? 2 : 1;
    }
    if (required > O32_CONVERSION_WIRE_LIMIT / 2)
        return conversion_error(O32_ERROR_NOT_ENOUGH_MEMORY);
    if (!capacity)
        return (LONG)required;
    if ((size_t)capacity < required)
        return conversion_error(O32_ERROR_INSUFFICIENT_BUFFER);
    size_t written = 0;
    for (size_t at = 0; at < bytes;)
    {
        BOOL invalid = FALSE;
        uint32 point;
        if (page == 3)
            point = utf8_point((uint8 *)input, bytes, &at, &invalid);
        else
        {
            uint8 value = (uint8)input[at++];
            point = value < 128 ? value : lx_host_sbcs_unicode[page][value - 128];
        }
        size_t needed = point >= 0x10000 ? 2 : 1;
        if (written > (size_t)capacity || needed > (size_t)capacity - written)
            return conversion_error(O32_ERROR_INSUFFICIENT_BUFFER);
        if (point >= 0x10000)
        {
            lx_guest_store_u16((uint8 *)output + 2 * written++, 0xd800 + ((point - 0x10000) >> 10));
            point = 0xdc00 + ((point - 0x10000) & 1023);
        }
        lx_guest_store_u16((uint8 *)output + 2 * written++, (uint16)point);
    }
    return (LONG)written;
}

LONG WideCharToMultiByte(ULONG codepage, ULONG flags, PVOID input, LONG count, PSZ output,
                         LONG capacity, PSZ default_text, PULONG used_default)
{
    TRACE_NATIVE("WideCharToMultiByte(%u, %X, %p, %d, %p, %d, %p, %p)", codepage, flags, input,
                 count, output, capacity, default_text, used_default);
    if (!input || !count || count < -1 || (capacity && output == input))
        return conversion_error(O32_ERROR_INVALID_PARAMETER);
    if ((flags & ~0x270u) || ((flags & O32_WC_DISCARDNS) && (flags & O32_WC_DEFAULTCHAR)) ||
        (!(flags & O32_WC_COMPOSITECHECK) && (flags & 0x70)))
        return conversion_error(O32_ERROR_INVALID_FLAGS);
    size_t replacement_bytes = 1;
    const uint8 *replacement = (const uint8 *)"?";
    if (default_text)
    {
        replacement_bytes = 0;
        while (replacement_bytes < 14 && default_text[replacement_bytes])
            replacement_bytes++;
        if (!replacement_bytes || replacement_bytes > 13)
            return conversion_error(O32_ERROR_INTERNAL_ERROR);
        replacement = (const uint8 *)default_text;
    }
    if (capacity < 0)
        return conversion_error(O32_ERROR_INSUFFICIENT_BUFFER);
    if (capacity && !output)
        return conversion_error(O32_ERROR_INVALID_PARAMETER);
    int page = conversion_page(codepage);
    if (page < 0 || flags || (page == 3 && default_text))
        return conversion_error(O32_ERROR_NOT_SUPPORTED);
    size_t units = 0;
    if (count == -1)
    {
        while (units < O32_CONVERSION_WIRE_LIMIT / 2 &&
               lx_guest_load_u16((uint8 *)input + 2 * units))
            units++;
        if (units == O32_CONVERSION_WIRE_LIMIT / 2)
            return conversion_error(O32_ERROR_NOT_ENOUGH_MEMORY);
        units++;
    }
    else
        units = (size_t)count;
    if (units > O32_CONVERSION_WIRE_LIMIT / 2 || (size_t)capacity > O32_CONVERSION_WIRE_LIMIT)
        return conversion_error(O32_ERROR_NOT_ENOUGH_MEMORY);
    if (capacity && overlaps(input, units * 2, output, (size_t)capacity))
        return conversion_error(O32_ERROR_INVALID_PARAMETER);
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
            if (page == 3)
            {
                length = utf8_bytes(point, bytes);
                used |= invalid;
            }
            else if (point < 128)
                bytes[0] = (uint8)point;
            else
            {
                unsigned found;
                for (found = 0; found < 128; found++)
                    if (lx_host_sbcs_unicode[page][found] == point)
                        break;
                if (found < 128)
                    bytes[0] = (uint8)(found + 128);
                else
                {
                    selected = replacement;
                    length = replacement_bytes;
                    used = TRUE;
                }
            }
            if (written > O32_CONVERSION_WIRE_LIMIT - length)
                return conversion_error(O32_ERROR_NOT_ENOUGH_MEMORY);
            if (pass)
            {
                if (written > (size_t)capacity || length > (size_t)capacity - written)
                    return conversion_error(O32_ERROR_INSUFFICIENT_BUFFER);
                memcpy(output + written, selected, length);
            }
            written += length;
        }
        if (!pass)
        {
            required = written;
            if (capacity && (size_t)capacity < required)
                return conversion_error(O32_ERROR_INSUFFICIENT_BUFFER);
        }
    }
    if (used_default)
        lx_guest_store_u32(used_default, used);
    return (LONG)required;
}
