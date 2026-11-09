/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

/* TCPIP32 entry: the generated export table plus the _ress data export and
   upper-case aliases of every export name. */
#include "os2/include/private/export.h"
#include "os2/include/sys/socket.h"
#include "os2/include/sys/select.h"
#include "os2/include/sys/time.h"
#include "os2/include/sys/uio.h"
#include "os2/include/sys/ioctl.h"
#include "os2/include/sys/sysctl.h"
#include "os2/include/sys/itypes.h"
#include "os2/include/netinet/in.h"
#include "os2/include/arpa/inet.h"
#include "os2/include/netdb.h"
#include "os2/include/resolv.h"
#include "os2/include/unistd.h"
#include "host/socket.h"
#include "os2/mptn/common/network.h"
#include "os2/mptn/common/socket_errors.h"
#include "os2/mptn/tcpip32/internal.h"
#include <ctype.h>

#define lxNativeModuleInit lxTcpipGeneratedInit
#define lxNativeModuleDeinit lxTcpipGeneratedDeinit
#include "os2/mptn/tcpip32/generated/tcpip32-lx.h"
#undef lxNativeModuleInit
#undef lxNativeModuleDeinit

#if LX_LEGACY
OS2EXPORT void lxNativeModuleDeinit(void)
{
    lx_tcpip_resolver_terminate();
    lxTcpipGeneratedDeinit();
}

enum
{
    EXPORT_NAME_LIMIT = 128,
    EXPORT_NAME_SIZE = 64,
    /* TCPIP32 exports the _ress resolver state at this ordinal. */
    RESS_ORDINAL = 143
};

OS2EXPORT const LxExport *lxNativeModuleInit(uint32 *count)
{
    /* Each export may gain an upper-case alias, so the table holds twice the names. */
    static LxExport exports[2 * EXPORT_NAME_LIMIT];
    static char uppercase[EXPORT_NAME_LIMIT][EXPORT_NAME_SIZE];
    uint32 n = 0;
    const LxExport *generated = lxTcpipGeneratedInit(&n);
    if (!generated || n >= EXPORT_NAME_LIMIT || !lx_tcpip_resolver_initialize())
        return NULL;
    memcpy(exports, generated, n * sizeof *exports);
    exports[n++] = (LxExport){RESS_ORDINAL, "_ress", (void *)(uintptr_t)lx_tcpip_resolver_address(),
                              NULL, LX_EXPORT_ABSOLUTE};
    unsigned original = n;
    for (unsigned i = 0; i < original; i++)
    {
        if (!exports[i].name || strlen(exports[i].name) >= EXPORT_NAME_SIZE)
            continue;
        size_t j;
        for (j = 0; exports[i].name[j]; j++)
            uppercase[i][j] = (char)toupper((unsigned char)exports[i].name[j]);
        uppercase[i][j] = 0;
        if (strcmp(uppercase[i], exports[i].name))
        {
            exports[n] = exports[i];
            exports[n].ordinal = 0;
            exports[n++].name = uppercase[i];
        }
    }
    *count = n;
    return exports;
}
#endif
