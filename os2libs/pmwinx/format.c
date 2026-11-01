/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows integer/string formatting consumes 32-bit argument words. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#include <stdarg.h>
#include <stdio.h>
typedef BOOL (*ReadArgument)(void *, ULONG *);
typedef struct GuestArguments
{
    uint32 address;
} GuestArguments;
static BOOL guest_argument(void *state, ULONG *value)
{
    GuestArguments *args = state;
    if (!args->address || args->address > UINT32_MAX - 4 ||
        !lx_execution_read(GLoaderState.execution, args->address, value, 4))
        return FALSE;
    args->address += 4;
    return TRUE;
}
static BOOL native_argument(void *state, ULONG *value)
{
    va_list *args = state;
    *value = va_arg(*args, ULONG);
    return TRUE;
}
static char *argument_string(ULONG address, LONG precision)
{
    if (!address)
        return strdup("(null)");
    unsigned limit = precision >= 0 ? (unsigned)precision : 1024;
    if (limit > 1024)
        limit = 1024;
    char *text = malloc((size_t)limit + 1);
    if (!text)
        return NULL;
    for (unsigned i = 0; i < limit; i++)
    {
        if ((uint64)address + i > UINT32_MAX ||
            !lx_execution_read(GLoaderState.execution, address + i, text + i, 1))
        {
            free(text);
            return NULL;
        }
        if (!text[i])
            return text;
    }
    text[limit] = 0;
    if (precision >= 0)
        return text;
    free(text);
    return NULL;
}
static LONG format_words(char *output, const char *format, ReadArgument read, void *arguments)
{
    size_t used = 0;
    for (const char *p = format; *p; p++)
    {
        if (*p != '%')
        {
            if (used == 1023)
                goto too_long;
            output[used++] = *p;
            continue;
        }
        p++;
        if (*p == '%')
        {
            if (used == 1023)
                goto too_long;
            output[used++] = '%';
            continue;
        }
        char flags[8] = {0};
        unsigned nflags = 0;
        while (*p && strchr("-+ #0", *p))
        {
            if (!strchr(flags, *p))
                flags[nflags++] = *p;
            p++;
        }
        ULONG word;
        LONG width = 0, precision = -1;
        if (*p == '*')
        {
            if (!read(arguments, &word))
                goto invalid;
            width = (LONG)word;
            if (width < -1023 || width > 1023)
                goto too_long;
            if (width < 0)
            {
                width = -width;
                if (!strchr(flags, '-'))
                    flags[nflags++] = '-';
            }
            p++;
        }
        else
            while (*p >= '0' && *p <= '9')
            {
                width = width * 10 + *p++ - '0';
                if (width > 1023)
                    goto too_long;
            }
        if (*p == '.')
        {
            p++;
            precision = 0;
            if (*p == '*')
            {
                if (!read(arguments, &word))
                    goto invalid;
                precision = (LONG)word;
                if (precision < 0)
                    precision = -1;
                p++;
            }
            else
                while (*p >= '0' && *p <= '9')
                {
                    precision = precision * 10 + *p++ - '0';
                    if (precision > 1023)
                        goto too_long;
                }
            if (precision > 1023)
                goto too_long;
        }
        BOOL short_value = *p == 'h', long_value = *p == 'l';
        if (short_value || long_value)
            p++;
        char kind = *p;
        if (!kind || !strchr("diuoxXcsp", kind) || (long_value && (kind == 's' || kind == 'c')))
            goto invalid;
        if (!read(arguments, &word))
            goto invalid;
        if (short_value)
            word = kind == 'd' || kind == 'i' ? (ULONG)(LONG)(SHORT)word : (USHORT)word;
        char specification[48], width_text[12] = "", precision_text[16] = "";
        if (kind == 'p' && !width)
        {
            width = 8;
            if (!strchr(flags, '0'))
                flags[nflags++] = '0';
        }
        if (width)
            snprintf(width_text, sizeof width_text, "%d", width);
        if (precision >= 0)
            snprintf(precision_text, sizeof precision_text, ".%d", precision);
        snprintf(specification, sizeof specification, "%%%s%s%s%c", flags, width_text,
                 precision_text, kind == 'p' ? 'X' : kind);
        int count;
        char *text = NULL;
        if (kind == 's')
        {
            text = argument_string(word, precision);
            if (!text)
                goto invalid;
            count = snprintf(output + used, 1024 - used, specification, text);
        }
        else if (kind == 'd' || kind == 'i')
            count = snprintf(output + used, 1024 - used, specification, (int32_t)word);
        else if (kind == 'c')
            count = snprintf(output + used, 1024 - used, specification, (int)(word & 255));
        else
            count = snprintf(output + used, 1024 - used, specification, (unsigned)word);
        free(text);
        if (count < 0 || (size_t)count >= 1024 - used)
            goto too_long;
        used += (size_t)count;
    }
    output[used] = 0;
    return (LONG)used;
invalid:
    open32SetError(87);
    return -1;
too_long:
    open32SetError(122);
    return -1;
}
LONG open32FormatGuest(PSZ format, ULONG arguments, PSZ output)
{
    GuestArguments state = {arguments};
    return format_words(output, format, guest_argument, &state);
}
static char *format_snapshot(PSZ format)
{
    uint32 address;
    if (!format || !GLoaderState.execution ||
        !lx_execution_address(GLoaderState.execution, format, 1, LX_GUEST_READ, &address))
        return NULL;
    return lx_execution_copy_string(GLoaderState.execution, address, 4096);
}
static LONG write_output(PSZ output, const char *formatted, LONG count)
{
    uint32 address;
    if (count >= 0 &&
        (!output ||
         !lx_execution_address(GLoaderState.execution, output, (size_t)count + 1, LX_GUEST_WRITE,
                               &address) ||
         !lx_execution_write(GLoaderState.execution, address, formatted, (size_t)count + 1)))
    {
        open32SetError(87);
        return -1;
    }
    return count;
}
LONG wsprintf(PSZ output, PSZ format, ...)
{
    char *copy = format_snapshot(format), formatted[1024];
    if (!copy)
    {
        open32SetError(87);
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    LONG count = format_words(formatted, copy, native_argument, &arguments);
    va_end(arguments);
    free(copy);
    return write_output(output, formatted, count);
}
LONG wvsprintf(PSZ output, PSZ format, PVOID arguments)
{
    char *copy = format_snapshot(format), formatted[1024];
    if (!copy || (uintptr_t)arguments > UINT32_MAX)
    {
        free(copy);
        open32SetError(87);
        return -1;
    }
    LONG count = open32FormatGuest(copy, (ULONG)(uintptr_t)arguments, formatted);
    free(copy);
    return write_output(output, formatted, count);
}
