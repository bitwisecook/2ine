/* Public domain. No rights reserved. Wholly AI-authored. */
/* The clipboard and the system atom table, which names clipboard formats,
   share one lock. */
#ifndef TWOINE_PM_CLIPBOARD_H
#define TWOINE_PM_CLIPBOARD_H
#include "os2/ps/pmmerge/pmwin/internal.h"
#include "host/sync.h"

extern LX_PRIVATE LxHostMutex *clipboard_mutex;
LX_PRIVATE BOOL lock_clipboard(void);
LX_PRIVATE const char *systemAtomName(ULONG format);
#endif
