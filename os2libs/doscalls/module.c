/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#include "os2libs/doscalls/internal.h"
#include "os2libs/generated/doscalls-lx.h"

LX_PRIVATE LxHostMutex *GMutexDosCalls;
LX_PRIVATE LxHostMutex *GMutexDosBeep;
LX_PRIVATE LxHostMutex *GMutexDir16;

LX_PRIVATE ExitListItem *GExitList = NULL;

LX_PRIVATE HFileInfo *HFiles = NULL;
LX_PRIVATE uint32 MaxHFiles = 0;

LX_PRIVATE Thread *GThreads;
LX_PRIVATE uint32 GNextThreadId = 2;
LX_PRIVATE uint32 GNextThreadStack = 0x18000000;

LX_PRIVATE SharedMemBlock *GSharedMem = NULL;

LX_PRIVATE NamedSem *GNamedSems = NULL;

/* CPPRMI36 opens its unnamed heap event by an empty name; retain the latest such event. */
LX_PRIVATE EventSem *GLastUnnamedEventSem = NULL;
LX_PRIVATE uint32 GLastUnnamedEventHandle;

LX_PRIVATE DirFinder GHDir1;

LX_PRIVATE GINFOSEG *ginfo;
LX_PRIVATE SEL ginfosel;
LX_PRIVATE LINFOSEG *linfo;
LX_PRIVATE SEL linfosel;

/* Legacy directory IDs index private typed handles; 1 is the system search. */
LX_PRIVATE HDIR hdir16[0x10000];

#if !LX_LEGACY
ULONG DosFlatToSel(VOID)
{
    return 0;
}
#endif

LX_PRIVATE ChildProcess *GChildren;

#if !LX_LEGACY
ULONG DosSelToFlat(VOID)
{
    return 0;
}
#endif

LX_PRIVATE DosBeepGeneratorInfo dos_beep_info;

LX_PRIVATE LxHostOnce GCritSecOnce = LX_HOST_ONCE_INIT;
LX_PRIVATE LxHostMutex *GCritSec;

LX_NATIVE_CONSTRUCTOR(doscalls)
{
    GLoaderState.beep = DosBeep;
    GLoaderState.dosExit = DosExit;
    GLoaderState.writeFile = DosWrite;
    GLoaderState.getResource = DosGetResource;
    GLoaderState.queryResourceSize = DosQueryResourceSize;
    GLoaderState.freeResource = DosFreeResource;

    if (!(GMutexDosCalls = lx_host_mutex_create(0, 0)))
    {
        fprintf(stderr, "Host mutex allocation failed!\n");
        abort();
    }

    if (!(GMutexDosBeep = lx_host_mutex_create(0, 0)))
    {
        lx_host_mutex_destroy(GMutexDosCalls);
        fprintf(stderr, "Host mutex allocation failed!\n");
        abort();
    }

    if (!(GMutexDir16 = lx_host_mutex_create(0, 0)))
        abort();

    MaxHFiles = 20;
    HFiles = (HFileInfo *)malloc(sizeof(HFileInfo) * MaxHFiles);
    if (!HFiles)
    {
        lx_host_mutex_destroy(GMutexDosCalls);
        lx_host_mutex_destroy(GMutexDosBeep);
        fprintf(stderr, "Out of memory!\n");
        abort();
    }

    HFileInfo *info = HFiles;
    for (uint32 i = 0; i < MaxHFiles; i++, info++)
    {
        info->handle = 0;
        info->type = 0;
        info->attr = 0;
        info->flags = 0;
    }

    FIXME("We can iterate /proc/self/fd to get all currently-open files");
    for (int i = 0; i <= 2; i++)
    {
        initFileInfo(lx_host_file_standard(i), &HFiles[i], 0);
    }

    FIXME("Maybe set this up in lx_loader and make it NULL in lib2ine");
    FIXME("ginfo's time fields update in a timer interrupt!  :O");
    ginfo = (GINFOSEG *)GLoaderState.allocSegment(&ginfosel, 0);
    linfo = (LINFOSEG *)GLoaderState.allocSegment(&linfosel, 0);
    if ((!ginfo) || (!linfo))
    {
        lx_host_mutex_destroy(GMutexDosCalls);
        lx_host_mutex_destroy(GMutexDosBeep);
        fprintf(stderr, "Failed to allocate info segments!\n");
        abort();
    }

    memset(ginfo, '\0', sizeof(GINFOSEG));
    memset(linfo, '\0', sizeof(LINFOSEG));
    const uint64 now = lx_host_realtime_us() / 1000000;
    LxHostCalendar value = {0};
    lx_host_calendar((int64_t)now, 1, &value);
    ginfo->time = (uint32)now;
    ginfo->hour = value.hour;
    ginfo->minutes = value.minute;
    ginfo->seconds = value.second;
    ginfo->timezone = 0;
    ginfo->cusecTimerInterval = 100;
    ginfo->day = value.day;
    ginfo->month = value.month;
    ginfo->year = value.year;
    ginfo->weekday = value.weekday;
    ginfo->uchMajorVersion = 20;
    FIXME("are these backwards?");
    ginfo->uchMinorVersion = 40;
    FIXME("are these backwards?");
    ginfo->sgCurrent = 1;
    ginfo->sgMax = 99;
    ginfo->cHugeShift = 0;
    ginfo->fProtectModeOnly = 1;
    ginfo->pidForeground = GLoaderState.pib.pib_ulpid;
    ginfo->csecMaxWait = 1;
    ginfo->cmsecMinSlice = 1;
    ginfo->cmsecMaxSlice = 10;
    ginfo->bootdrive = 3;
    ginfo->csgWindowableVioMax = 99;
    ginfo->csgPMMax = 99;
    linfo->pidCurrent = GLoaderState.pib.pib_ulpid;
    linfo->pidParent = GLoaderState.pib.pib_ulppid;
}

LX_NATIVE_DESTRUCTOR(doscalls)
{
    GLoaderState.dosExit = NULL;
    GLoaderState.writeFile = NULL;
    GLoaderState.getResource = NULL;
    GLoaderState.queryResourceSize = NULL;
    GLoaderState.freeResource = NULL;
    destroyModuleResources();

    ExitListItem *next = GExitList;
    GExitList = NULL;

    for (ExitListItem *item = next; item; item = next)
    {
        next = item->next;
        free(item);
    }

    for (uint32 i = 0; i < MaxHFiles; i++)
    {
        if (HFiles[i].handle > 1)
            lx_handle_close(HFiles[i].handle, LX_HANDLE_FILE);
    }
    free(HFiles);
    HFiles = NULL;
    MaxHFiles = 0;

    GLoaderState.freeSelector(ginfosel);
    GLoaderState.freeSelector(linfosel);
    lx_host_mutex_destroy(GMutexDosCalls);
    lx_host_mutex_destroy(GMutexDosBeep);
    lx_host_mutex_destroy(GMutexDir16);
}
