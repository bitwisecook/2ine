/* Public domain. No rights reserved. Wholly AI-authored. */
/* Windows integer/string formatting consumes 32-bit argument words. */
#include "os2/include/private/export.h"
#include "os2/ps/awe/internal.h"
#include <stdarg.h>
#include <stdio.h>

enum
{
    /* wsprintf writes at most 1024 bytes, including the terminator, as on Windows. */
    WSPRINTF_BUFFER_SIZE = 1024,
    WSPRINTF_TEXT_MAX = WSPRINTF_BUFFER_SIZE - 1,
    /* Every argument, whatever its conversion, is one 32-bit stack word. */
    ARGUMENT_WORD_SIZE = 4,
    /* %p prints the 32-bit address as eight hex digits. */
    POINTER_HEX_DIGITS = 8,
    FORMAT_LENGTH_MAX = 4096
};

typedef BOOL (*ReadArgument)(void *, ULONG *);

typedef struct GuestArguments
{
    uint32 address;
} GuestArguments;

static BOOL guest_argument(void *state, ULONG *value)
{
    GuestArguments *args = state;
    if (!args->address || args->address > UINT32_MAX - ARGUMENT_WORD_SIZE ||
        !lx_execution_read(GProcessState.execution, args->address, value, ARGUMENT_WORD_SIZE))
        return FALSE;
    args->address += ARGUMENT_WORD_SIZE;
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
    /* An unterminated string longer than the whole output cannot be formatted. */
    unsigned limit = precision >= 0 ? (unsigned)precision : WSPRINTF_BUFFER_SIZE;
    if (limit > WSPRINTF_BUFFER_SIZE)
        limit = WSPRINTF_BUFFER_SIZE;
    char *text = malloc((size_t)limit + 1);
    if (!text)
        return NULL;
    for (unsigned i = 0; i < limit; i++)
    {
        if ((uint64)address + i > UINT32_MAX ||
            !lx_execution_read(GProcessState.execution, address + i, text + i, 1))
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
            if (used == WSPRINTF_TEXT_MAX)
                goto too_long;
            output[used++] = *p;
            continue;
        }
        p++;
        if (*p == '%')
        {
            if (used == WSPRINTF_TEXT_MAX)
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
            if (width < -WSPRINTF_TEXT_MAX || width > WSPRINTF_TEXT_MAX)
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
                if (width > WSPRINTF_TEXT_MAX)
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
                    if (precision > WSPRINTF_TEXT_MAX)
                        goto too_long;
                }
            if (precision > WSPRINTF_TEXT_MAX)
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
            width = POINTER_HEX_DIGITS;
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
            count = snprintf(output + used, WSPRINTF_BUFFER_SIZE - used, specification, text);
        }
        else if (kind == 'd' || kind == 'i')
            count =
                snprintf(output + used, WSPRINTF_BUFFER_SIZE - used, specification, (int32_t)word);
        else if (kind == 'c')
            count = snprintf(output + used, WSPRINTF_BUFFER_SIZE - used, specification,
                             (int)(word & 255));
        else
            count =
                snprintf(output + used, WSPRINTF_BUFFER_SIZE - used, specification, (unsigned)word);
        free(text);
        if (count < 0 || (size_t)count >= WSPRINTF_BUFFER_SIZE - used)
            goto too_long;
        used += (size_t)count;
    }
    output[used] = 0;
    return (LONG)used;
invalid:
    open32SetError(ERROR_INVALID_PARAMETER);
    return -1;
too_long:
    open32SetError(ERROR_INSUFFICIENT_BUFFER);
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
    if (!format || !GProcessState.execution ||
        !lx_execution_address(GProcessState.execution, format, 1, LX_GUEST_READ, &address))
        return NULL;
    return lx_execution_copy_string(GProcessState.execution, address, FORMAT_LENGTH_MAX);
}

static LONG write_output(PSZ output, const char *formatted, LONG count)
{
    uint32 address;
    if (count >= 0 &&
        (!output ||
         !lx_execution_address(GProcessState.execution, output, (size_t)count + 1, LX_GUEST_WRITE,
                               &address) ||
         !lx_execution_write(GProcessState.execution, address, formatted, (size_t)count + 1)))
    {
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    return count;
}

LONG wsprintf(PSZ output, PSZ format, ...)
{
    char *copy = format_snapshot(format), formatted[WSPRINTF_BUFFER_SIZE];
    if (!copy)
    {
        open32SetError(ERROR_INVALID_PARAMETER);
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
    char *copy = format_snapshot(format), formatted[WSPRINTF_BUFFER_SIZE];
    if (!copy || (uintptr_t)arguments > UINT32_MAX)
    {
        free(copy);
        open32SetError(ERROR_INVALID_PARAMETER);
        return -1;
    }
    LONG count = open32FormatGuest(copy, (ULONG)(uintptr_t)arguments, formatted);
    free(copy);
    return write_output(output, formatted, count);
}
