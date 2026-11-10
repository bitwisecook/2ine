/* Public domain. No rights reserved. Wholly AI-authored. */
/* The clipboard and the system atom table, which names clipboard formats,
   share one lock. */
#ifndef TWOINE_PM_CLIPBOARD_H
#define TWOINE_PM_CLIPBOARD_H
#include "os2/include/private/pmwinp.h"
#include "host/sync.h"

/* The system atom table names registered clipboard formats; atoms from FIRST_STRING_ATOM are
   strings, and lower values are integer atoms. */
enum
{
    SYSTEM_ATOM_TABLE = 1,
    FIRST_STRING_ATOM = 0xc000
};

extern LX_PRIVATE LxHostMutex *clipboard_mutex;
/* Acquires clipboard_mutex; on TRUE the caller releases it with lx_host_mutex_unlock. */
LX_PRIVATE BOOL lock_clipboard(void);
/* The name of a format registered in the system atom table, or NULL; requires the lock. */
LX_PRIVATE const char *systemAtomName(ULONG format);
#endif
