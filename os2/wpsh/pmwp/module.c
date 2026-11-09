/* Public domain. No rights reserved. Wholly AI-authored. */
/* PMWP entry: the export table, the class creation entries (*NewClass) and the
   guest addresses of the exported class data. */
#include "os2/include/private/export.h"
#include "os2/include/pmwin.h"
#include "os2/include/pmshl.h"
#include "os2/include/pmwp.h"
#include "os2/include/private/pmp.h"
#include "os2/wpsh/pmwp/internal.h"
#include "os2/include/bsedos.h"
#include "emulator/abi.h"
#include "host/fs.h"
#include "host/sync.h"
#include <ctype.h>
#include <strings.h>
#include "os2/wpsh/pmwp/generated/pmwp-lx.h"

ULONG WPFilesystemNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPFILESYSTEM, major, minor);
}

ULONG WPFolderNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPFOLDER, major, minor);
}

ULONG WPDataFileNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPDATAFILE, major, minor);
}

ULONG WPObjectNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPOBJECT, major, minor);
}

ULONG WPAbstractNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPABSTRACT, major, minor);
}

ULONG WPTransientNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPTRANSIENT, major, minor);
}

ULONG WPDesktopNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPDESKTOP, major, minor);
}

ULONG WPClassManagerNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_WPCLASSMANAGER, major, minor);
}

ULONG M_WPFolderNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_M_WPFOLDER, major, minor);
}

ULONG M_WPDataFileNewClass(ULONG major, ULONG minor)
{
    return pmwpClassNew(PMWP_M_WPDATAFILE, major, minor);
}

#if LX_LEGACY
static uint32 exported_data(void *host)
{
    uint32 address = 0;
    return lx_execution_address(GProcessState.execution, host, 1, LX_GUEST_READ, &address) ? address
                                                                                           : 0;
}

uint32 pmwpExportedClassData(PmwpClassId id)
{
    switch (id)
    {
        case PMWP_WPFILESYSTEM:
            return exported_data(data_address_WPFilesystemClassData);
        case PMWP_WPFOLDER:
            return exported_data(data_address_WPFolderClassData);
        case PMWP_WPOBJECT:
            return exported_data(data_address_WPObjectClassData);
        case PMWP_WPDISK:
            return exported_data(data_address_WPDiskClassData);
        case PMWP_WPPROGRAM:
            return exported_data(data_address_WPProgramClassData);
        case PMWP_M_WPFILESYSTEM:
            return exported_data(data_address_M_WPFileSystemClassData);
        case PMWP_M_WPOBJECT:
            return exported_data(data_address_M_WPObjectClassData);
        default:
            return 0;
    }
}
#else
uint32 pmwpExportedClassData(PmwpClassId id)
{
    (void)id;
    return 0;
}
#endif
