/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */
#ifndef TWOINE_OS2LIBS_DOSCALLS_INTERNAL_H
#define TWOINE_OS2LIBS_DOSCALLS_INTERNAL_H
/* TWOINE_TRACE_RET logs results independently of argument tracing. */
#define TRACE_RET(...)                                                                             \
    do                                                                                             \
    {                                                                                              \
        if (getenv("TWOINE_TRACE_RET"))                                                            \
        {                                                                                          \
            fprintf(stderr, "2INE RET: ");                                                         \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n");                                                                 \
        }                                                                                          \
    } while (0)
// DosSearchPath() flags, from the OS/2 Toolkit's bsedos.h.
#define SEARCH_PATH 0x0000          // pszPathOrName is a path list.
#define SEARCH_CUR_DIRECTORY 0x0001 // search the current directory too.
#define SEARCH_ENVIRONMENT 0x0002   // pszPathOrName names an environment variable.
#define SEARCH_IGNORENETERRS 0x0004 // ignore an unset environment variable.
// DosCopy control bits (OS/2 Toolkit bsedos.h).
#define DCPY_EXISTING 0x0001U // copy even if the target exists.
#define DCPY_APPEND 0x0002U   // append instead of replacing.
#include "os2libs/os2native16.h"
#include "os2libs/doscalls.h"
#include "hal/host.h"
#include "hal/host_fs.h"
#include "hal/host_process.h"
#include "hal/host_sync.h"
#include <limits.h>

typedef struct ExitListItem
{
    PFNEXITLIST host_entry;
    uint32 guest_entry;
    uint32 priority;
    LxModule *module;
    struct ExitListItem *next;
} ExitListItem;

// Flags returned for character devices from DosQueryHType()...
enum
{
    DAW_STDIN = (1 << 0),
    DAW_STDOUT = (1 << 1),
    DAW_NUL = (1 << 2),
    DAW_CLOCK = (1 << 3),
    DAW_SPEC = (1 << 4),
    DAW_ADD_ON = (1 << 5),
    DAW_GIOCTL = (1 << 6),

    DAW_LEVEL0 = 0,
    DAW_LEVEL1 = (1 << 7),
    DAW_LEVEL2 = (1 << 8),
    DAW_LEVEL3 = (1 << 7) | (1 << 8),
    DAW_FCNLEV = (1 << 7) | (1 << 8) | (1 << 9),

    DAW_OPENCLOSE = (1 << 11),
    DAW_SHARE = (1 << 12),
    DAW_NONIBM = (1 << 13),
    DAW_IDC = (1 << 14),
    DAW_CHARACTER = (1 << 15)
};

typedef struct HFileInfo
{
    uint32 handle; /* Private typed file handle; zero=free, one=reserved. */
    ULONG type;    // file, pipe, device, etc.
    ULONG attr;    // for character devices, DAW_*
    ULONG flags;   // OPEN_FLAGS_*
} HFileInfo;

typedef struct Thread
{
    LxHostThread *thread;
    LxGuestMapping *stack;
    void *invocation;
    uint32 id, handle;
    uint16 selector;
    int finished, joining;
    struct Thread *next;
} Thread;

typedef struct
{
    LxHostMonitor *monitor;
    int posted;
} EventSem;

/* The registry maps case-folded OS/2 names to private semaphore handles. */
/* Registry names own one private handle; retire it after the last public close. */
typedef struct NamedSem
{
    char *name;
    int is_mutex;
    uint32 internal_handle;
    void *handle;
    struct NamedSem *next;
} NamedSem;

/* Named blocks use shared host backing; retired private blocks remain mapped until exit. */
typedef struct SharedMemBlock
{
    void *addr;
    size_t size;
    LxGuestMapping *mapping;
    int freed;
    struct SharedMemBlock *next;
} SharedMemBlock;

typedef struct
{
    LxHostDirectory *dirp;
    ULONG attr;
    ULONG level;
    char pattern[CCHMAXPATHCOMP];
} DirFinder;

enum ResourceOperation
{
    RESOURCE_READ,
    RESOURCE_WRITE,
    RESOURCE_REQUEST_MUTEX,
    RESOURCE_RELEASE_MUTEX,
    RESOURCE_RESET_EVENT,
    RESOURCE_POST_EVENT,
    RESOURCE_WAIT_EVENT,
    RESOURCE_QUERY_EVENT
};

typedef struct ResourceCall
{
    enum ResourceOperation operation;
    uint32 handle, value;
    void *resource, *buffer, *output;
} ResourceCall;

typedef struct ChildProcess
{
    LxHostChild *host;
    int waiting;
    struct ChildProcess *next;
} ChildProcess;

typedef struct DosBeepGeneratorInfo
{
    ULONG samples_remaining;
    ULONG positive;
    ULONG square_remaining;
    ULONG square_length;
    ULONG square_remainder;
    ULONG accumulator;
    ULONG next_freq;
    ULONG next_duration;
} DosBeepGeneratorInfo;

typedef struct NeThreadStart
{
    uint32 entry, stack;
    void *invocation;
    uint16 tid, tib_selector;
} NeThreadStart;
extern LX_PRIVATE LxHostMutex *GMutexDosCalls;
extern LX_PRIVATE LxHostMutex *GMutexDosBeep;
extern LX_PRIVATE LxHostMutex *GMutexDir16;
extern LX_PRIVATE ExitListItem *GExitList;
extern LX_PRIVATE HFileInfo *HFiles;
extern LX_PRIVATE uint32 MaxHFiles;
extern LX_PRIVATE Thread *GThreads;
extern LX_PRIVATE uint32 GNextThreadId;
extern LX_PRIVATE uint32 GNextThreadStack;
extern LX_PRIVATE SharedMemBlock *GSharedMem;
extern LX_PRIVATE NamedSem *GNamedSems;
extern LX_PRIVATE EventSem *GLastUnnamedEventSem;
extern LX_PRIVATE uint32 GLastUnnamedEventHandle;
extern LX_PRIVATE DirFinder GHDir1;
extern LX_PRIVATE GINFOSEG *ginfo;
extern LX_PRIVATE SEL ginfosel;
extern LX_PRIVATE LINFOSEG *linfo;
extern LX_PRIVATE SEL linfosel;
extern LX_PRIVATE HDIR hdir16[65536];
extern LX_PRIVATE ChildProcess *GChildren;
extern LX_PRIVATE DosBeepGeneratorInfo dos_beep_info;
extern LX_PRIVATE LxHostOnce GCritSecOnce;
extern LX_PRIVATE LxHostMutex *GCritSec;
LX_PRIVATE void destroyEventResource(void *value);
LX_PRIVATE void destroyMutexResource(void *value);
LX_PRIVATE NamedSem *findNamedSemLocked(const char *name, int is_mutex);
LX_PRIVATE APIRET publishSemaphore(void *value, LxHandleKind kind, const char *name,
                                   uint32 *output);
LX_PRIVATE APIRET openSemaphore(const char *name, LxHandleKind kind, uint32 *output);
LX_PRIVATE void retireSemaphoreName(void *value, LxHandleKind kind);
LX_PRIVATE void releaseResourceBorrow(void *p);
LX_PRIVATE void destroyDirectoryResource(void *value);
LX_PRIVATE int grabLock(LxHostMutex **lock);
LX_PRIVATE void ungrabLock(LxHostMutex **lock);
LX_PRIVATE APIRET fileError(LxHostFsError error);
LX_PRIVATE void destroyFileResource(void *value);
LX_PRIVATE int initFileInfo(LxHostFile *file, HFileInfo *info, ULONG flags);
LX_PRIVATE APIRET DosQueryModuleNameObject(HMODULE hmod, ULONG buflen, PCHAR buf,
                                           const LxModule *lxmod);
LX_PRIVATE uint32 resourceOperation(void *argument);
LX_PRIVATE uint32 guardedResource(enum ResourceOperation operation, uint32 handle, uint32 value,
                                  LxHandleBorrow *object, void *buffer, void *output);
LX_PRIVATE int borrowFile(HFILE id, LxHandleBorrow *object);
LX_PRIVATE APIRET DosWrite_implementation(HFILE id, PVOID data, ULONG size, PULONG actual);
LX_PRIVATE void runDosExitList(const uint32 why);
LX_PRIVATE VOID DosExit_implementation(ULONG action, ULONG exitcode);
ULONG _DosFlatToSel(PVOID ptr);
LX_PRIVATE APIRET DosQueryHType_implementation(HFILE hFile, PULONG pType, PULONG pAttr);
LX_PRIVATE char *makeUnixPath(const char *os2path, APIRET *err);
LX_PRIVATE APIRET doDosOpen(PSZ name, PHFILE output, PULONG action, LONGLONG size, ULONG attributes,
                            ULONG open_flags, ULONG open_mode, PEAOP2 extended);
LX_PRIVATE APIRET DosRequestMutexSemResource(HMTX hmtx, ULONG ulTimeout, LxHostMutex *mutex);
LX_PRIVATE APIRET DosReleaseMutexSemResource(HMTX hmtx, LxHostMutex *mutex);
LX_PRIVATE APIRET DosSetFilePtr_implementation(HFILE id, LONG distance, ULONG origin,
                                               PULONG output);
LX_PRIVATE APIRET DosRead_implementation(HFILE id, PVOID data, ULONG size, PULONG actual);
LX_PRIVATE APIRET DosClose_implementation(HFILE id);
LX_PRIVATE void unixTimeToOs2(const int64_t seconds, FDATE *date, FTIME *time);
LX_PRIVATE void initFileCreationDateTime(FDATE *fdate, FTIME *ftime);
LX_PRIVATE APIRET queryFileInfoStandardFromStat(const LxHostFileInfo *statbuf, PVOID pInfoBuf,
                                                ULONG cbInfoBuf);
LX_PRIVATE APIRET queryPathInfoStandard(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf);
LX_PRIVATE APIRET queryPathInfoEaSize(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf);
LX_PRIVATE APIRET queryPathInfoFullName(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf);
LX_PRIVATE APIRET queryPathInfoEasFromList(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf);
LX_PRIVATE APIRET queryPathInfo(PSZ unixPath, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf);
LX_PRIVATE APIRET queryFileInfoStandard(const LxHostFileInfo *statbuf, PVOID pInfoBuf,
                                        ULONG cbInfoBuf);
LX_PRIVATE APIRET queryFileInfoEaSize(const LxHostFileInfo *statbuf, PVOID pInfoBuf,
                                      ULONG cbInfoBuf);
LX_PRIVATE APIRET queryFileInfoEasFromList(const LxHostFileInfo *statbuf, PVOID pInfoBuf,
                                           ULONG cbInfoBuf);
LX_PRIVATE void destroyThreadResource(void *value);
LX_PRIVATE void os2ThreadCleanup(void *arg);
LX_PRIVATE void *os2ThreadEntry(void *arg);
LX_PRIVATE char *findExePath(const char *exe, APIRET *err);
LX_PRIVATE char **calcEnvp(const char *os2env, const char *exe, APIRET *err);
LX_PRIVATE char **calcArgv(const char *os2args, const char *progpath, APIRET *err);
LX_PRIVATE void setProcessResultCode(PRESULTCODES output, const LxHostChildStatus *status);
LX_PRIVATE APIRET DosExecPgm_implementation(PCHAR pObjname, LONG cbObjname, ULONG execFlag,
                                            PSZ pArg, PSZ pEnv, PRESULTCODES pRes, PSZ pName);
LX_PRIVATE APIRET DosResetEventSemResource(HEV hev, PULONG pulPostCt, EventSem *sem);
LX_PRIVATE APIRET DosPostEventSemResource(HEV hev, EventSem *sem);
LX_PRIVATE APIRET DosWaitEventSemResource(HEV hev, ULONG timeout, EventSem *sem);
LX_PRIVATE APIRET DosQueryEventSemResource(HEV hev, PULONG pulPostCt, EventSem *sem);
LX_PRIVATE void makeSharedMemName(const char *os2name, char *buf, const size_t buflen);
LX_PRIVATE APIRET mapNamedSharedMemory(PPVOID output, const char *name, ULONG size, ULONG flags);
LX_PRIVATE APIRET DosDelete_implementation(PSZ name);
LX_PRIVATE APIRET DosQueryProcAddrObject(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN *ppfn,
                                         const LxModule *lxmod);
LX_PRIVATE ULONG findNextSizeNeeded(const ULONG level, const ULONG count);
LX_PRIVATE int wildcardMatch(const char *str, const char *pattern);
LX_PRIVATE int findNextOne(DirFinder *dirf, PVOID *ppfindbuf, PULONG pcbBuf, int *err);
LX_PRIVATE APIRET findNext(DirFinder *dirf, PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames);
LX_PRIVATE APIRET DosFindFirstLocked(PSZ pszFileSpec, PHDIR phdir, ULONG flAttribute,
                                     PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames,
                                     ULONG ulInfoLevel);
LX_PRIVATE APIRET DosFreeModuleObject(HMODULE hmod, LxModule *lxmod);
LX_PRIVATE APIRET DosQueryCurrentDir_implementation(ULONG disknum, PBYTE pBuf, PULONG pcbBuf);
LX_PRIVATE APIRET DosQueryCurrentDisk_implementation(PULONG pdisknum, PULONG pdiskmap);
LX_PRIVATE APIRET DosSetCurrentDir_implementation(PSZ pszName);
LX_PRIVATE APIRET DosCreateDir_implementation(PSZ pszDirName, PEAOP2 peaop2);
ULONG _DosSelToFlat(void *ptr);
LX_PRIVATE int semaphoreWord(PHSEM16 sem, int exchange, uint32 value, uint32 *previous);
LX_PRIVATE APIRET16 waitSemaphoreWord(PHSEM16 sem, LONG milliseconds, int request);
LX_PRIVATE int mutexCanClose(void *value);
LX_PRIVATE APIRET DosDupHandle_implementation(HFILE source, PHFILE output);
LX_PRIVATE int DosBeepGenerator(void *userdata, float *stream, int samples, int freq);
LX_PRIVATE APIRET DosBeep_implementation(ULONG freq, ULONG duration);
LX_PRIVATE void initCritSec(void);
LX_PRIVATE void storeDateTime16(uint8 *data, FDATE date, FTIME time);
LX_PRIVATE void neThreadCleanup(void *argument);
LX_PRIVATE void *neThreadEntry(void *argument);
LX_PRIVATE void destroyModuleResources(void);
#endif
