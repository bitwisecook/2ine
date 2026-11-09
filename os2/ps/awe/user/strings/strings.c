/* Public domain. No rights reserved. Wholly AI-authored. */
/* ANSI string copies retain guest pointers and validate each destination extent. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include "os2/ps/awe/user/strings/case_tables.h"
#include "os2/include/pmwin.h"
#define STRING_LIMIT (1024u * 1024u)

static BOOL convert_buffer(ULONG source, ULONG destination, ULONG count, ULONG from, ULONG to)
{
    if (!count)
        return TRUE;
    if (count > STRING_LIMIT)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint8 *copy = malloc(count);
    if (!copy)
    {
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    BOOL result = lx_execution_read(GProcessState.execution, source, copy, count);
    if (result)
    {
        for (ULONG i = 0; i < count; i++)
            copy[i] = WinCpTranslateChar(0, from, copy[i], to);
        result = lx_execution_write(GProcessState.execution, destination, copy, count);
    }
    free(copy);
    if (!result)
        open32SetError(ERROR_INVALID_PARAMETER);
    return result;
}

static BOOL convert_string(ULONG source, ULONG destination, ULONG from, ULONG to)
{
    char *text = lx_execution_copy_string(GProcessState.execution, source, STRING_LIMIT);
    if (!text)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ULONG count = (ULONG)strlen(text) + 1;
    free(text);
    return convert_buffer(source, destination, count, from, to);
}

BOOL OemToChar(ULONG source, ULONG destination)
{
    return convert_string(source, destination, 437, 1004);
}

BOOL CharToOem(ULONG source, ULONG destination)
{
    return convert_string(source, destination, 1004, 437);
}

BOOL OemToCharBuff(ULONG source, ULONG destination, ULONG count)
{
    return convert_buffer(source, destination, count, 437, 1004);
}

BOOL CharToOemBuff(ULONG source, ULONG destination, ULONG count)
{
    return convert_buffer(source, destination, count, 1004, 437);
}

static ULONG string_copy(ULONG destination, ULONG source, BOOL append)
{
    char *input = lx_execution_copy_string(GProcessState.execution, source, STRING_LIMIT);
    char *prefix =
        append ? lx_execution_copy_string(GProcessState.execution, destination, STRING_LIMIT)
               : NULL;
    if (!input || (append && !prefix))
    {
        free(input);
        free(prefix);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    size_t length = strlen(input), before = prefix ? strlen(prefix) : 0;
    if ((uint64_t)destination + before + length + 1 > UINT64_C(0x100000000))
    {
        free(input);
        free(prefix);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GProcessState.execution, destination + (ULONG)before, length + 1,
                             LX_GUEST_WRITE, &output))
    {
        free(input);
        free(prefix);
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    memcpy(output.host, input, length);
    ((char *)output.host)[length] = 0;
    lx_execution_release(&output);
    free(input);
    free(prefix);
    return destination;
}

ULONG lstrcpy(ULONG destination, ULONG source)
{
    return string_copy(destination, source, FALSE);
}

ULONG lstrcat(ULONG destination, ULONG source)
{
    return string_copy(destination, source, TRUE);
}

LONG lstrlen(ULONG source)
{
    if (!source)
        return 0;
    char *text = lx_execution_copy_string(GProcessState.execution, source, STRING_LIMIT);
    if (!text)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    LONG length = (LONG)strlen(text);
    free(text);
    return length;
}

static ULONG case_buffer(ULONG pointer, ULONG count, const uint8 table[256])
{
    if (!count)
        return 0;
    LxGuestBorrow text = {0};
    if (count > STRING_LIMIT || !lx_execution_borrow(GProcessState.execution, pointer, count,
                                                     LX_GUEST_READ | LX_GUEST_WRITE, &text))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    uint8 *bytes = text.host;
    for (ULONG i = 0; i < count; i++)
        bytes[i] = table[bytes[i]];
    lx_execution_release(&text);
    return count;
}

static ULONG case_string(ULONG value, const uint8 table[256])
{
    if (value <= UINT16_MAX)
        return (value & 0xff00) | table[value & 255];
    char *copy = lx_execution_copy_string(GProcessState.execution, value, STRING_LIMIT);
    if (!copy)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    ULONG count = (ULONG)strlen(copy) + 1;
    free(copy);
    return case_buffer(value, count, table) ? value : 0;
}

ULONG CharUpper(ULONG value)
{
    return case_string(value, upper_case);
}

ULONG CharLower(ULONG value)
{
    return case_string(value, lower_case);
}

ULONG CharUpperBuff(ULONG pointer, ULONG count)
{
    return case_buffer(pointer, count, upper_case);
}

ULONG CharLowerBuff(ULONG pointer, ULONG count)
{
    return case_buffer(pointer, count, lower_case);
}

ULONG CharNext(ULONG pointer)
{
    uint8 value;
    if (!lx_execution_read(GProcessState.execution, pointer, &value, 1))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return value ? pointer + 1 : pointer;
}

ULONG CharPrev(ULONG start, ULONG current)
{
    if (current <= start)
        return current;
    if (!lx_execution_memory(GProcessState.execution, current - 1, 1, LX_GUEST_READ))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* The process NLS provider exposes no double-byte lead ranges. */
    return current - 1;
}

LONG lstrcmpi(ULONG first, ULONG second)
{
    char *a = lx_execution_copy_string(GProcessState.execution, first, STRING_LIMIT);
    char *b = lx_execution_copy_string(GProcessState.execution, second, STRING_LIMIT);
    LONG result = 0;
    if (!a || !b)
        open32SetError(ERROR_INVALID_PARAMETER);
    else
    {
        const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
        for (;; p++, q++)
        {
            unsigned int x = *p, y = *q;
            if (x >= 'a' && x <= 'z')
                x -= 'a' - 'A';
            if (y >= 'a' && y <= 'z')
                y -= 'a' - 'A';
            result = (LONG)x - (LONG)y;
            if (result || !*p)
                break;
        }
    }
    free(a);
    free(b);
    return result;
}

LONG lstrcmp(ULONG first, ULONG second)
{
    char *a = lx_execution_copy_string(GProcessState.execution, first, STRING_LIMIT);
    char *b = lx_execution_copy_string(GProcessState.execution, second, STRING_LIMIT);
    LONG result = 0;
    if (!a || !b)
        open32SetError(ERROR_INVALID_PARAMETER);
    else
    {
        const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
        while (*p && *p == *q)
        {
            p++;
            q++;
        }
        result = (LONG)*p - (LONG)*q;
    }
    free(a);
    free(b);
    return result;
}

static ULONG character_type(ULONG character)
{
    if (character > 255)
        return 0;
    char byte = (char)character;
    uint8 flags[2];
    return GetStringTypeEx(0x409, 1, &byte, 1, flags) ? lx_guest_load_u16(flags) : 0;
}

BOOL IsCharUpper(ULONG character)
{
    return (character_type(character) & 1) != 0;
}

BOOL IsCharAlpha(ULONG character)
{
    return (character_type(character) & 0x100) != 0;
}

BOOL IsCharAlphaNumeric(ULONG character)
{
    return (character_type(character) & 0x104) != 0;
}

LONG lstrcmp2(PSZ first, PSZ second)
{
    if (!first || !second)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* Both operands are compared as UTF-16 units from the OEM code page. */
    ULONG page = GetOEMCP();
    LONG a = MultiByteToWideChar(page, 1, first, -1, NULL, 0);
    LONG b = MultiByteToWideChar(page, 1, second, -1, NULL, 0);
    uint8 *left = a > 0 ? malloc((size_t)a * 2) : NULL;
    uint8 *right = b > 0 ? malloc((size_t)b * 2) : NULL;
    LONG result = 0;
    if (!left || !right || !MultiByteToWideChar(page, 1, first, -1, left, a) ||
        !MultiByteToWideChar(page, 1, second, -1, right, b))
        open32SetError(ERROR_NOT_ENOUGH_MEMORY);
    else
        for (LONG i = 0; i < a && i < b && !result; i++)
        {
            USHORT x = lx_guest_load_u16(left + i * 2), y = lx_guest_load_u16(right + i * 2);
            result = x < y ? -1 : x > y;
        }
    free(left);
    free(right);
    return result;
}
