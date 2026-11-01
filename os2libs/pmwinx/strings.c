/* Public domain. No rights reserved. Wholly AI-authored. */
/* ANSI string copies retain guest pointers and validate each destination extent. */
#include "os2libs/os2native.h"
#include "os2libs/pmwinx/internal.h"
#define STRING_LIMIT (1024u * 1024u)
static ULONG string_copy(ULONG destination, ULONG source, BOOL append)
{
    char *input = lx_execution_copy_string(GLoaderState.execution, source, STRING_LIMIT);
    char *prefix =
        append ? lx_execution_copy_string(GLoaderState.execution, destination, STRING_LIMIT) : NULL;
    if (!input || (append && !prefix))
    {
        free(input);
        free(prefix);
        open32SetError(87);
        return 0;
    }
    size_t length = strlen(input), before = prefix ? strlen(prefix) : 0;
    if ((uint64_t)destination + before + length + 1 > UINT64_C(0x100000000))
    {
        free(input);
        free(prefix);
        open32SetError(87);
        return 0;
    }
    LxGuestBorrow output = {0};
    if (!lx_execution_borrow(GLoaderState.execution, destination + (ULONG)before, length + 1,
                             LX_GUEST_WRITE, &output))
    {
        free(input);
        free(prefix);
        open32SetError(87);
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
    char *text = lx_execution_copy_string(GLoaderState.execution, source, STRING_LIMIT);
    if (!text)
    {
        open32SetError(87);
        return 0;
    }
    LONG length = (LONG)strlen(text);
    free(text);
    return length;
}
