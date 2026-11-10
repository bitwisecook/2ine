/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
/* DOSCALLS initialisation: handle tables, standard streams and info segments. */
#include "os2/baseos/doscalls/internal.h"
#include "os2/include/bsesub.h"
#include "os2/baseos/doscalls/generated/doscalls-lx.h"

LX_PRIVATE LxHostMutex *GMutexDosCalls;
LX_PRIVATE LxHostMutex *GMutexDosBeep;
LX_PRIVATE LxHostMutex *GMutexDir16;

/* A new process has OS/2's default 20 file handles; 0 to 2 are the standard streams. */
enum
{
    INITIAL_FILE_HANDLES = 20,
    STANDARD_HANDLE_COUNT = 3
};

/* GINFOSEG values for the 16-bit API. */
enum
{
    /* In tenths of a millisecond. */
    INFOSEG_TIMER_INTERVAL = 100,
    INFOSEG_MINOR_VERSION = 40,
    INFOSEG_SESSION_LIMIT = 99,
    INFOSEG_MAX_SLICE_MS = 10,
    /* Drive 3 is C:. */
    INFOSEG_BOOT_DRIVE_C = 3
};

LX_PRIVATE ExitListItem *GExitList = NULL;

LX_PRIVATE HFileInfo *HFiles = NULL;
LX_PRIVATE uint32 MaxHFiles = 0;

LX_PRIVATE Thread *GThreads;
/* Thread 1 is the main thread. */
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
    GProcessState.beep = DosBeep;
    GProcessState.dosExit = DosExit;
    GProcessState.getResource = DosGetResource;
    GProcessState.queryResourceSize = DosQueryResourceSize;
    GProcessState.freeResource = DosFreeResource;

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
    initializeThreadSuspension();

    MaxHFiles = INITIAL_FILE_HANDLES;
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
        info->type = FHT_DISKFILE;
        info->attr = 0;
        info->flags = 0;
    }

    FIXME("We can iterate /proc/self/fd to get all currently-open files");
    for (int i = 0; i < STANDARD_HANDLE_COUNT; i++)
    {
        initFileInfo(lx_host_file_standard(i), &HFiles[i], 0);
    }

    FIXME("Maybe set this up in lx_loader and make it NULL in lib2ine");
    FIXME("ginfo's time fields update in a timer interrupt!  :O");
    ginfo = (GINFOSEG *)GProcessState.allocSegment(&ginfosel, 0);
    linfo = (LINFOSEG *)GProcessState.allocSegment(&linfosel, 0);
    /* Host-side users of PM libraries load this module with no guest to read info segments. */
    if ((!ginfo || !linfo) && !GProcessState.execution)
    {
        if (ginfo)
            GProcessState.freeSelector(ginfosel);
        if (linfo)
            GProcessState.freeSelector(linfosel);
        ginfo = NULL;
        linfo = NULL;
        ginfosel = linfosel = 0;
        GProcessState.freeSharedMemory = DosFreeMem;
        return;
    }
    if ((!ginfo) || (!linfo))
    {
        lx_host_mutex_destroy(GMutexDosCalls);
        lx_host_mutex_destroy(GMutexDosBeep);
        fprintf(stderr, "Failed to allocate info segments!\n");
        abort();
    }

    memset(ginfo, '\0', sizeof(GINFOSEG));
    memset(linfo, '\0', sizeof(LINFOSEG));
    const uint64 now = lx_host_realtime_us() / US_PER_SECOND;
    LxHostCalendar value = {0};
    lx_host_calendar((int64_t)now, 1, &value);
    ginfo->time = (uint32)now;
    ginfo->hour = value.hour;
    ginfo->minutes = value.minute;
    ginfo->seconds = value.second;
    ginfo->timezone = 0;
    ginfo->cusecTimerInterval = INFOSEG_TIMER_INTERVAL;
    ginfo->day = value.day;
    ginfo->month = value.month;
    ginfo->year = value.year;
    ginfo->weekday = value.weekday;
    ginfo->uchMajorVersion = OS2_VERSION_MAJOR;
    FIXME("are these backwards?");
    ginfo->uchMinorVersion = INFOSEG_MINOR_VERSION;
    FIXME("are these backwards?");
    ginfo->sgCurrent = 1;
    ginfo->sgMax = INFOSEG_SESSION_LIMIT;
    ginfo->cHugeShift = 0;
    ginfo->fProtectModeOnly = 1;
    ginfo->pidForeground = GProcessState.pib.process_id;
    ginfo->csecMaxWait = 1;
    ginfo->cmsecMinSlice = 1;
    ginfo->cmsecMaxSlice = INFOSEG_MAX_SLICE_MS;
    ginfo->bootdrive = INFOSEG_BOOT_DRIVE_C;
    ginfo->csgWindowableVioMax = INFOSEG_SESSION_LIMIT;
    ginfo->csgPMMax = INFOSEG_SESSION_LIMIT;
    linfo->pidCurrent = GProcessState.pib.process_id;
    linfo->pidParent = GProcessState.pib.parent_id;
    GProcessState.freeSharedMemory = DosFreeMem;
}

LX_NATIVE_DESTRUCTOR(doscalls)
{
    if (GProcessState.freeSharedMemory == DosFreeMem)
        GProcessState.freeSharedMemory = NULL;
    cleanupTimers();
    cleanupSuballocation();
    cleanupThreadSuspension();
    GProcessState.dosExit = NULL;
    GProcessState.getResource = NULL;
    GProcessState.queryResourceSize = NULL;
    GProcessState.freeResource = NULL;
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

    if (ginfo)
        GProcessState.freeSelector(ginfosel);
    if (linfo)
        GProcessState.freeSelector(linfosel);
    lx_host_mutex_destroy(GMutexDosCalls);
    lx_host_mutex_destroy(GMutexDosBeep);
    lx_host_mutex_destroy(GMutexDir16);
}
