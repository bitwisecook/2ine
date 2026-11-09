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

OS2EXPORT const LxExport *lxNativeModuleInit(uint32 *count)
{
    static LxExport exports[256];
    static char uppercase[128][64];
    uint32 n = 0;
    const LxExport *generated = lxTcpipGeneratedInit(&n);
    if (!generated || n >= 128 || !lx_tcpip_resolver_initialize())
        return NULL;
    memcpy(exports, generated, n * sizeof *exports);
    exports[n++] = (LxExport){143, "_ress", (void *)(uintptr_t)lx_tcpip_resolver_address(), NULL,
                              LX_EXPORT_ABSOLUTE};
    unsigned original = n;
    for (unsigned i = 0; i < original; i++)
    {
        if (!exports[i].name || strlen(exports[i].name) >= 64)
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
