/**
 * 2ine; an OS/2 emulator for Linux.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#include "os2native16.h"
#include "doscalls.h"
#include "../runtime/host.h"
#include "../runtime/host_fs.h"
#include "../runtime/host_process.h"
#include "../runtime/host_sync.h"

#include <limits.h>

#include "doscalls-lx.h"

static LxHostMutex *GMutexDosCalls;
static LxHostMutex *GMutexDosBeep;
static LxHostMutex *GMutexDir16;

static char *makeUnixPath(const char *os2path, APIRET *err);

// Optional per-call result tracing (TWOINE_TRACE_RET), separate from the
//  argument trace so a failing call is easy to spot in a long tool run.
#define TRACE_RET(...) do { if (getenv("TWOINE_TRACE_RET")) { \
    fprintf(stderr, "2INE RET: "); fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); } } while (0)
static int grabLock(LxHostMutex **lock);
static void ungrabLock(LxHostMutex **lock);

typedef struct ExitListItem
{
    PFNEXITLIST host_entry;
    uint32 guest_entry;
    uint32 priority;
    struct ExitListItem *next;
} ExitListItem;

static ExitListItem *GExitList = NULL;

// Flags returned for character devices from DosQueryHType()...
enum {
    DAW_STDIN  = (1 << 0),
    DAW_STDOUT = (1 << 1),
    DAW_NUL = (1 << 2),
    DAW_CLOCK = (1 << 3),
    DAW_SPEC = (1 << 4),
    DAW_ADD_ON = (1 << 5),
    DAW_GIOCTL  = (1 << 6),

    DAW_LEVEL0 = 0,
    DAW_LEVEL1 = (1 << 7),
    DAW_LEVEL2 = (1 << 8),
    DAW_LEVEL3 = (1 << 7) | (1 << 8),
    DAW_FCNLEV = (1 << 7) | (1 << 8) | (1 << 9),

    // ((1 << 10) is reserved, I think.)

    DAW_OPENCLOSE = (1 << 11),
    DAW_SHARE = (1 << 12),
    DAW_NONIBM = (1 << 13),
    DAW_IDC = (1 << 14),
    DAW_CHARACTER = (1 << 15)
};

typedef struct HFileInfo
{
    uint32 handle; /* Private typed file handle; zero=free, one=reserved. */
    ULONG type;   // file, pipe, device, etc.
    ULONG attr;   // for character devices, DAW_*
    ULONG flags;  // OPEN_FLAGS_*
} HFileInfo;

static HFileInfo *HFiles = NULL;
static uint32 MaxHFiles = 0;

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
static Thread *GThreads;
static uint32 GNextThreadId = 2;
static uint32 GNextThreadStack = 0x18000000;



typedef struct
{
    LxHostMonitor *monitor;
    int posted;
} EventSem;

// Named semaphores are shared by the OS/2 processes 2ine starts. The registry
//  keeps the OS/2 name alongside the handle so DosOpenEventSem()/
//  DosOpenMutexSem() can find an existing object by name.
typedef struct NamedSem
{
    char *name;
    int is_mutex;
    uint32 internal_handle;
    void *handle;
    struct NamedSem *next;
} NamedSem;


// Named shared memory is backed by POSIX shared memory so that the OS/2
//  processes 2ine starts (a compiler driver and its preprocessor, for
//  example) really see the same pages. The registry also tracks anonymous
//  page-rounded DosAllocMem blocks so DosFreeMem can retire them without
//  breaking the OS/2 guarantee that a freed page stays addressable.
typedef struct SharedMemBlock
{
    void *addr;
    size_t size;
    LxGuestMapping *mapping;
    int freed;
    struct SharedMemBlock *next;
} SharedMemBlock;

static SharedMemBlock *GSharedMem = NULL;

static NamedSem *GNamedSems = NULL;

// The IBM C runtime's heap create/open pair (CPPRMI36 _ucreate/_uopen)
//  creates an unnamed event semaphore and then opens it by the (empty) name
//  stored in the heap header. OS/2 rejects that, but the runtime expects the
//  open to find the semaphore it just made, so remember the most recent
//  unnamed one for the empty-name open.
static EventSem *GLastUnnamedEventSem = NULL;
static uint32 GLastUnnamedEventHandle;

static void destroyEventResource(void *value)
{
    EventSem *sem = value;
    lx_host_monitor_destroy(sem->monitor); free(sem);
}
static void destroyMutexResource(void *value)
{
    LxHostMutex *mutex = value;
    /* An unpublished, initially-owned mutex can arrive here on allocation failure. */
    if (lx_host_mutex_busy(mutex))
        while (lx_host_mutex_unlock(mutex) == LX_HOST_LOCK_OK) {}
    lx_host_mutex_destroy(mutex);
}
static NamedSem *findNamedSemLocked(const char *name, int is_mutex)
{
    for (NamedSem *sem = GNamedSems; sem; sem = sem->next)
        if (sem->is_mutex == is_mutex && !strcasecmp(sem->name, name)) return sem;
    return NULL;
}
static APIRET publishSemaphore(void *value, LxHandleKind kind, const char *name, uint32 *output)
{
    void (*destroy)(void *) = kind == LX_HANDLE_MUTEX ? destroyMutexResource : destroyEventResource;
    uint32 public_handle = lx_handle_create(kind, value, destroy);
    if (!public_handle) { destroy(value); return ERROR_NOT_ENOUGH_MEMORY; }
    APIRET result = NO_ERROR;
    grabLock(&GMutexDosCalls);
    if (name && *name) {
        if (findNamedSemLocked(name, kind == LX_HANDLE_MUTEX)) result = ERROR_DUPLICATE_NAME;
        else {
            NamedSem *entry = calloc(1, sizeof *entry);
            if (entry) entry->name = strdup(name);
            uint32 internal = entry && entry->name ? lx_handle_create(kind, value, destroy) : 0;
            if (!internal) {
                if (entry) { free(entry->name); free(entry); }
                result = ERROR_NOT_ENOUGH_MEMORY;
            } else {
                entry->is_mutex = kind == LX_HANDLE_MUTEX; entry->handle = value;
                entry->internal_handle = internal; entry->next = GNamedSems; GNamedSems = entry;
            }
        }
    } else if (!name && kind == LX_HANDLE_EVENT) {
        uint32 internal = lx_handle_create(kind, value, destroy);
        if (!internal) result = ERROR_NOT_ENOUGH_MEMORY;
        else {
            uint32 old = GLastUnnamedEventHandle;
            GLastUnnamedEventSem = value; GLastUnnamedEventHandle = internal;
            if (old) lx_handle_close(old, LX_HANDLE_EVENT);
        }
    }
    ungrabLock(&GMutexDosCalls);
    if (result) lx_handle_close(public_handle, kind);
    else *output = public_handle;
    return result;
}
static APIRET openSemaphore(const char *name, LxHandleKind kind, uint32 *output)
{
    if (!output || (kind == LX_HANDLE_MUTEX && !name)) return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    void *value = NULL;
    if ((!name || !*name) && kind == LX_HANDLE_EVENT) value = GLastUnnamedEventSem;
    else if (name) {
        NamedSem *entry = findNamedSemLocked(name, kind == LX_HANDLE_MUTEX);
        if (entry) value = entry->handle;
    }
    uint32 handle = value ? lx_handle_create(kind, value,
        kind == LX_HANDLE_MUTEX ? destroyMutexResource : destroyEventResource) : 0;
    ungrabLock(&GMutexDosCalls);
    if (!value) return ERROR_SEM_NOT_FOUND;
    if (!handle) return ERROR_NOT_ENOUGH_MEMORY;
    *output = handle; return NO_ERROR;
}
/* Registry names own one private handle; retire it after the last public close. */
static void retireSemaphoreName(void *value, LxHandleKind kind)
{
    if (lx_handle_count(kind, value) != 1) return;
    for (NamedSem **link = &GNamedSems; *link; link = &(*link)->next) {
        NamedSem *entry = *link;
        if (entry->handle == value && entry->is_mutex == (kind == LX_HANDLE_MUTEX)) {
            *link = entry->next; uint32 internal = entry->internal_handle;
            free(entry->name); free(entry); lx_handle_close(internal, kind); return;
        }
    }
    if (kind == LX_HANDLE_EVENT && GLastUnnamedEventSem == value) {
        uint32 internal = GLastUnnamedEventHandle;
        GLastUnnamedEventHandle = 0; GLastUnnamedEventSem = NULL;
        lx_handle_close(internal, kind);
    }
}
static void releaseResourceBorrow(void *p) { lx_handle_release(p); }


typedef struct
{
    LxHostDirectory *dirp;
    ULONG attr;
    ULONG level;
    char pattern[CCHMAXPATHCOMP];
} DirFinder;

static DirFinder GHDir1;
static void destroyDirectoryResource(void *value)
{
    DirFinder *directory = value;
    if (directory->dirp) lx_host_directory_close(directory->dirp);
    free(directory);
}

static GINFOSEG *ginfo;
static SEL ginfosel;
static LINFOSEG *linfo;
static SEL linfosel;

/* Legacy directory IDs index private typed handles; 1 is the system search. */
static HDIR hdir16[0x10000];

static int grabLock(LxHostMutex **lock)
{
    return (lx_host_mutex_lock(*lock, UINT32_MAX) == LX_HOST_LOCK_OK);
} // grabLock

static void ungrabLock(LxHostMutex **lock)
{
    lx_host_mutex_unlock(*lock);
} // ungrabLock

static APIRET fileError(LxHostFsError error)
{
    switch (error) {
        case LX_HOST_FS_OK: return NO_ERROR;
        case LX_HOST_FS_MISSING: return ERROR_FILE_NOT_FOUND;
        case LX_HOST_FS_NOT_DIRECTORY: return ERROR_PATH_NOT_FOUND;
        case LX_HOST_FS_NOT_EMPTY: return ERROR_DIR_NOT_EMPTY;
        case LX_HOST_FS_EXISTS: return ERROR_FILE_EXISTS;
        case LX_HOST_FS_NO_MEMORY: return ERROR_NOT_ENOUGH_MEMORY;
        case LX_HOST_FS_TOO_MANY_FILES: return ERROR_TOO_MANY_OPEN_FILES;
        case LX_HOST_FS_FULL: return ERROR_DISK_FULL;
        case LX_HOST_FS_NAME_LONG: return ERROR_FILENAME_EXCED_RANGE;
        case LX_HOST_FS_CROSS_DEVICE: return ERROR_NOT_SAME_DEVICE;
        case LX_HOST_FS_INVALID: return ERROR_INVALID_PARAMETER;
        case LX_HOST_FS_IO: return ERROR_INVALID_FUNCTION;
        default: return ERROR_ACCESS_DENIED;
    }
}
static void destroyFileResource(void *value) { lx_host_file_close(value); }
static int initFileInfo(LxHostFile *file, HFileInfo *info, ULONG flags)
{
    memset(info, 0, sizeof *info);
    LxHostFileInfo description;
    if (!file || lx_host_file_info(file, &description) != LX_HOST_FS_OK) {
        lx_host_file_close(file); return 0;
    }
    info->handle = lx_handle_create(LX_HANDLE_FILE, file, destroyFileResource);
    if (!info->handle) { lx_host_file_close(file); return 0; }
    info->type = description.kind == LX_HOST_FILE_REGULAR ? 0 : description.kind == LX_HOST_FILE_DEVICE ? 1 : 2;
    info->attr = DAW_STDIN | DAW_STDOUT | DAW_LEVEL1 | DAW_CHARACTER;
    info->flags = flags; return 1;
}
 // initHFileInfoFromUnixFd


APIRET DosGetInfoBlocks(PTIB *pptib, PPIB *pppib)
{
    TRACE_NATIVE("DosGetInfoBlocks(%p, %p)", pptib, pppib);

    if (pptib != NULL)
        *pptib = (PTIB) LX_GETTIB();

    if (pppib != NULL)
        *pppib = GLoaderState.guest_pib ? lx_execution_mapping_host(GLoaderState.guest_pib) : (PPIB) &GLoaderState.pib;

    return 0;
} // DosGetInfoBlocks

APIRET DosQuerySysInfo(ULONG first, ULONG last, PVOID _buf, ULONG buflen)
{
    TRACE_NATIVE("DosQuerySysInfo(%u, %u, %p, %u)", (uint) first, (uint) last, _buf, (uint) buflen);

    uint32 *buf = (uint32 *) _buf;
    if (last < first) return ERROR_INVALID_PARAMETER;
    if ( (buflen / sizeof (uint32)) < ((last - first) + 1) ) return ERROR_BUFFER_OVERFLOW;
    for (uint32 varid = first; varid <= last; varid++) {
        switch (varid) {
            case QSV_MAX_PATH_LENGTH: *(buf++) = PATH_MAX; break;
            case QSV_MAX_TEXT_SESSIONS: *(buf++) = 999999; break;
            case QSV_MAX_PM_SESSIONS: *(buf++) = 999999; break;
            case QSV_MAX_VDM_SESSIONS: *(buf++) = 0; break;
            case QSV_BOOT_DRIVE: *(buf++) = 3; break;  // "C:"
            case QSV_DYN_PRI_VARIATION: *(buf++) = 1; break;
            case QSV_MAX_WAIT: *(buf++) = 1; break;
            case QSV_MIN_SLICE: *(buf++) = 1; break;
            case QSV_MAX_SLICE: *(buf++) = 10; break;
            case QSV_PAGE_SIZE: *(buf++) = 4096; break;
            // !!! FIXME: change the version number in some way so apps can know this isn't actually OS/2.
            case QSV_VERSION_MAJOR: *(buf++) = 20; break;   // OS/2 Warp 4.5
            case QSV_VERSION_MINOR: *(buf++) = 45; break;   // OS/2 Warp 4.5
            case QSV_VERSION_REVISION: *(buf++) = 0; break; // OS/2 Warp 4.5

            case QSV_MS_COUNT: *(buf++) = (uint32) (lx_host_monotonic_us() / 1000); break;

            case QSV_TIME_LOW: *(buf++) = (uint32) (lx_host_realtime_us() / 1000000); break;
            case QSV_TIME_HIGH: *(buf++) = 0; break;  // !!! FIXME: 32-bit Linux time_t is 32-bits!

            case QSV_TOTPHYSMEM:
            case QSV_TOTRESMEM:
            case QSV_TOTAVAILMEM: {
                uint64 total = 0, available = 0;
                lx_host_system_memory(&total, &available);
                uint64 value = (varid == QSV_TOTPHYSMEM) ? total :
                               (varid == QSV_TOTAVAILMEM) ? available : total - available;
                *(buf++) = value > UINT32_MAX ? UINT32_MAX : (uint32) value;
                break;
            }

            case QSV_MAXPRMEM: *(buf++) = 0x7FFFFFFF; break;  // !!! FIXME: should I list all 4 gigs? Also: what about machines with < 2 gigs?
            case QSV_MAXSHMEM: *(buf++) = 0x7FFFFFFF; break;  // !!! FIXME: should I list all 4 gigs? Also: what about machines with < 2 gigs?

            case QSV_TIMER_INTERVAL: *(buf++) = 10; break;
            case QSV_MAX_COMP_LENGTH: *(buf++) = NAME_MAX; break;
            case QSV_FOREGROUND_FS_SESSION: *(buf++) = 1; break;  // !!! FIXME
            case QSV_FOREGROUND_PROCESS: *(buf++) = 1; break;  // !!! FIXME

            default: return ERROR_INVALID_PARAMETER;
        } // switch
    } // for

    return NO_ERROR;
} // DosQuerySysInfo


static APIRET DosQueryModuleNameObject(HMODULE hmod, ULONG buflen, PCHAR buf, const LxModule *lxmod)
{
    TRACE_NATIVE("DosQueryModuleName(%u, %u, %p)", (uint) hmod, (uint) buflen, buf);


    // !!! FIXME: error 6 ERROR_INVALID_HANDLE
    if (strlen(lxmod->os2path) >= buflen)
        return ERROR_BAD_LENGTH;
    strcpy(buf, lxmod->os2path);
    return NO_ERROR;
} // DosQueryModuleName
APIRET DosQueryModuleName(HMODULE hmod, ULONG buflen, PCHAR buf)
{
    LxHandleBorrow module;
    if (!lx_handle_borrow(hmod, LX_HANDLE_MODULE, &module)) return ERROR_INVALID_HANDLE;
    const APIRET result = DosQueryModuleNameObject(hmod, buflen, buf, module.resource);
    lx_handle_release(&module);
    return result;
}



APIRET DosScanEnv(PSZ name, PSZ *outval)
{
    TRACE_NATIVE("DosScanEnv('%s', %p)", name, outval);

    char *env = GLoaderState.pib.pib_pchenv;
    const size_t len = strlen(name);
    while (*env) {
        if ((strncmp(env, name, len) == 0) && (env[len] == '=')) {
            *outval = env + len + 1;
            return NO_ERROR;
        } // if
        env += strlen(env) + 1;
    } // while

    return ERROR_ENVVAR_NOT_FOUND;
} // DosScanEnv


// DosSearchPath() flags, from the OS/2 Toolkit's bsedos.h.
#define SEARCH_PATH           0x0000  // pszPathOrName is a path list.
#define SEARCH_CUR_DIRECTORY  0x0001  // search the current directory too.
#define SEARCH_ENVIRONMENT    0x0002  // pszPathOrName names an environment variable.
#define SEARCH_IGNORENETERRS  0x0004  // ignore an unset environment variable.

// Split an OS/2 path list (';'-separated), probe each directory for the given
//  file name, and return the first hit as an OS/2-style path. A file name
//  that is already a path (drive or separator) is probed directly.
APIRET DosSearchPath(ULONG flag, PSZ pszPathOrName, PSZ pszFilename, PBYTE pBuf, ULONG cbBuf)
{
    TRACE_NATIVE("DosSearchPath(%u, '%s', '%s', %p, %u)", (uint) flag,
                 pszPathOrName ? pszPathOrName : "",
                 pszFilename ? pszFilename : "", pBuf, (uint) cbBuf);

    if ((!pszFilename) || (!*pszFilename) || (!pBuf) || (!cbBuf))
        return ERROR_INVALID_PARAMETER;

    const char *file = pszFilename;
    char fullpath[PATH_MAX];
    APIRET err = NO_ERROR;

    // A file name that carries its own drive/absolute path bypasses the list.
    if (strchr(file, ':') || (strchr(file, '\\') && (*file == '\\')) ||
        (strchr(file, '/') && (*file == '/'))) {
        char *unixpath = makeUnixPath(file, &err);
        if (!unixpath)
            return err;
        const int found = lx_host_path_exists(unixpath);
        free(unixpath);
        if (!found)
            return ERROR_FILE_NOT_FOUND;
        if (strlen(file) + 1 > cbBuf)
            return ERROR_BUFFER_OVERFLOW;
        strcpy((char *) pBuf, file);
        return NO_ERROR;
    } // if

    // pszPathOrName is either a path list or the name of an environment
    //  variable holding one.
    char pathlist[PATH_MAX] = { 0 };
    const char *path = pszPathOrName;
    if (flag & SEARCH_ENVIRONMENT) {
        char *value = NULL;
        const APIRET envrc = (path && *path) ? DosScanEnv((PSZ) path, &value)
                                             : ERROR_ENVVAR_NOT_FOUND;
        if (envrc != NO_ERROR) {
            if (!(flag & SEARCH_IGNORENETERRS))
                return envrc;
            path = NULL;
        } else {
            strncpy(pathlist, value, sizeof (pathlist) - 1);
            path = pathlist;
        } // else
    } // if

    int found = 0;

    if (flag & SEARCH_CUR_DIRECTORY) {
        snprintf(fullpath, sizeof (fullpath), "%s", file);
        char *unixpath = makeUnixPath(fullpath, &err);
        if (unixpath) {
            found = lx_host_path_exists(unixpath);
            free(unixpath);
        } // if
    } // if

    const char *dir = path;
    while ((!found) && dir && *dir) {
        const char *end = strchr(dir, ';');
        const size_t dirlen = end ? (size_t) (end - dir) : strlen(dir);
        if (dirlen == 0) {
            snprintf(fullpath, sizeof (fullpath), "%s", file);
        } else if ((dirlen + 1 + strlen(file)) < sizeof (fullpath)) {
            snprintf(fullpath, sizeof (fullpath), "%.*s\\%s",
                     (int) dirlen, dir, file);
        } else {
            return ERROR_FILENAME_EXCED_RANGE;
        } // else

        char *unixpath = makeUnixPath(fullpath, &err);
        if (unixpath) {
            found = lx_host_path_exists(unixpath);
            free(unixpath);
        } // if

        dir = end ? end + 1 : NULL;
    } // while

    if (!found)
        return ERROR_FILE_NOT_FOUND;
    if (strlen(fullpath) + 1 > cbBuf)
        return ERROR_BUFFER_OVERFLOW;

    strcpy((char *) pBuf, fullpath);
    return NO_ERROR;
} // DosSearchPath


enum ResourceOperation { RESOURCE_READ, RESOURCE_WRITE, RESOURCE_REQUEST_MUTEX,
    RESOURCE_RELEASE_MUTEX, RESOURCE_RESET_EVENT, RESOURCE_POST_EVENT, RESOURCE_WAIT_EVENT, RESOURCE_QUERY_EVENT };
typedef struct ResourceCall {
    enum ResourceOperation operation;
    uint32 handle, value;
    void *resource, *buffer, *output;
} ResourceCall;
static APIRET DosRequestMutexSemResource(HMTX, ULONG, LxHostMutex *);
static APIRET DosReleaseMutexSemResource(HMTX, LxHostMutex *);
static APIRET DosResetEventSemResource(HEV, PULONG, EventSem *);
static APIRET DosPostEventSemResource(HEV, EventSem *);
static APIRET DosWaitEventSemResource(HEV, ULONG, EventSem *);
static APIRET DosQueryEventSemResource(HEV, PULONG, EventSem *);
static uint32 resourceOperation(void *argument)
{
    ResourceCall *call = argument;
    switch (call->operation) {
        case RESOURCE_READ: return lx_host_file_read(call->resource, call->buffer, call->value, call->output);
        case RESOURCE_WRITE: return lx_host_file_write(call->resource, call->buffer, call->value, call->output);
        case RESOURCE_REQUEST_MUTEX: return DosRequestMutexSemResource(call->handle, call->value, call->resource);
        case RESOURCE_RELEASE_MUTEX: return DosReleaseMutexSemResource(call->handle, call->resource);
        case RESOURCE_RESET_EVENT: return DosResetEventSemResource(call->handle, call->output, call->resource);
        case RESOURCE_POST_EVENT: return DosPostEventSemResource(call->handle, call->resource);
        case RESOURCE_WAIT_EVENT: return DosWaitEventSemResource(call->handle, call->value, call->resource);
        case RESOURCE_QUERY_EVENT: return DosQueryEventSemResource(call->handle, call->output, call->resource);
    }
    return ERROR_INVALID_FUNCTION;
}
static uint32 guardedResource(enum ResourceOperation operation, uint32 handle, uint32 value,
                               LxHandleBorrow *object, void *buffer, void *output)
{
    ResourceCall call = {operation, handle, value, object->resource, buffer, output};
    return lx_host_guarded_call(resourceOperation, &call, releaseResourceBorrow, object);
}
static int borrowFile(HFILE id, LxHandleBorrow *object)
{
    grabLock(&GMutexDosCalls);
    const int ok = id < MaxHFiles && HFiles[id].handle > 1 &&
        lx_handle_borrow(HFiles[id].handle, LX_HANDLE_FILE, object);
    ungrabLock(&GMutexDosCalls); return ok;
}
static APIRET DosWrite_implementation(HFILE id, PVOID data, ULONG size, PULONG actual)
{
    LxHandleBorrow file;
    if (!borrowFile(id, &file)) return ERROR_INVALID_HANDLE;
    size_t count = 0; LxHostFsError error;
    error = (LxHostFsError) guardedResource(RESOURCE_WRITE, id, size, &file, data, &count);
    if (actual) *actual = (ULONG) count;
    return fileError(error);
}

APIRET DosWrite(HFILE h, PVOID buf, ULONG buflen, PULONG actual)
{
    TRACE_NATIVE("DosWrite(%u, %p, %u, %p)", (uint) h, buf, (uint) buflen, actual);
    return DosWrite_implementation(h, buf, buflen, actual);
} // DosWrite

static void runDosExitList(const uint32 why)
{
    ExitListItem *next = NULL;
    for (ExitListItem *item = GExitList; item; item = next) {
        // don't run any of these more than once:
        //  http://www.verycomputer.com/3_c1f6e02c06ed108e_1.htm
        PFNEXITLIST host_entry = item->host_entry;
        uint32 guest_entry = item->guest_entry;
        next = item->next;
        GExitList = next;
        free(item);
        //printf("calling dosexitlist item %p\n", fn); fflush(stdout);
        if (GLoaderState.execution) lx_execution_callback32(GLoaderState.execution, guest_entry, why);
        else host_entry(why);
        //printf("dosexitlist item %p returned!\n", fn); fflush(stdout);
    } // for
} // runDosExitList

static VOID DosExit_implementation(ULONG action, ULONG exitcode)
{
    // !!! FIXME: what does a value other than 0 or 1 do here?
    if (action == EXIT_THREAD) {
        // !!! FIXME: If last thread: terminate process.
        FIXME("if last thread, treat as DosExit(1)");  // right now it will terminate process like exit(0), without running the exit list or reporting exitcode.
        lx_execution_end_thread(GLoaderState.execution);
        lx_host_thread_exit(NULL);  // Legacy 16-bit thread entry.
    } // if

    // terminate the process.
    runDosExitList(TC_EXIT);

    GLoaderState.terminate(exitcode);
} // DosExit_implementation

VOID DosExit(ULONG action, ULONG exitcode)
{
    TRACE_NATIVE("DosExit(%u, %u)", (uint) action, (uint) exitcode);
    DosExit_implementation(action, exitcode);
} // DosExit

APIRET DosExitList(ULONG ordercode, PFNEXITLIST fn)
{
    TRACE_NATIVE("DosExitList(%u, %p)", (uint) ordercode, fn);

    if (fn == NULL)
        return ERROR_INVALID_FUNCTION;

    const uint8 cmd = ordercode & 0xFF;
    const uint8 arg = (ordercode >> 8) & 0xFF;
    ExitListItem *prev = NULL;
    ExitListItem *item = NULL;

    // !!! FIXME: docs say this is illegal, but have to check what OS/2 actually does here.
    if ((cmd != EXLST_ADD) && (arg != 0))
        return ERROR_INVALID_DATA;

    switch (cmd) {
        case EXLST_ADD: {
            ExitListItem *newitem = (ExitListItem *) malloc(sizeof (ExitListItem));
            if (!newitem)
                return ERROR_NOT_ENOUGH_MEMORY;
            newitem->host_entry = GLoaderState.execution ? NULL : fn;
            newitem->guest_entry = GLoaderState.execution ? (uint32) (uintptr_t) fn : 0;
            newitem->priority = arg;
            for (item = GExitList; item; item = item->next) {
                if (item->priority >= ((uint32) arg))
                    break;
                prev = item;
            } // for
            if (prev) {
                prev->next = newitem;
                newitem->next = item;
            } else {
                newitem->next = GExitList;
                GExitList = newitem;
            } // else
            return NO_ERROR;
        } // case

        case EXLST_REMOVE: {
            for (item = GExitList; item; item = item->next) {
                if (GLoaderState.execution ? item->guest_entry == (uint32) (uintptr_t) fn : item->host_entry == fn) {
                    if (prev)
                        prev->next = item->next;
                    else
                        GExitList = item->next;
                    free(item);
                    return NO_ERROR;
                } // if
                prev = item;
            } // for
            return ERROR_INVALID_FUNCTION;  // !!! FIXME: yeah?
        } // case

        case EXLST_EXIT:
            return NO_ERROR;  // ... just treat this as a no-op, I guess...?

        default: return ERROR_INVALID_DATA;
    } // switch

    return NO_ERROR;
} // DosExitList

APIRET DosCreateEventSem(PSZ name, PHEV output, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateEventSem('%s', %p, %u, %u)", name, output, (uint) attr, (uint) state);
    if (!output) return ERROR_INVALID_PARAMETER;
    EventSem *sem = calloc(1, sizeof *sem);
    if (!sem) return ERROR_NOT_ENOUGH_MEMORY;
    sem->monitor = lx_host_monitor_create();
    if (!sem->monitor) { free(sem); return ERROR_NOT_ENOUGH_MEMORY; }
    sem->posted = state != 0;
    return publishSemaphore(sem, LX_HANDLE_EVENT, name, output);
}
APIRET DosOpenEventSem(PSZ name, PHEV output)
{
    TRACE_NATIVE("DosOpenEventSem('%s', %p)", name, output);
    return openSemaphore(name, LX_HANDLE_EVENT, output);
}
APIRET DosCreateMutexSem(PSZ name, PHMTX output, ULONG attr, BOOL32 state)
{
    TRACE_NATIVE("DosCreateMutexSem('%s', %p, %u, %u)", name, output, (uint) attr, (uint) state);
    if (!output) return ERROR_INVALID_PARAMETER;
    LxHostMutex *mutex = lx_host_mutex_create(1, state);
    if (!mutex) return ERROR_NOT_ENOUGH_MEMORY;
    return publishSemaphore(mutex, LX_HANDLE_MUTEX, name, output);
}
APIRET DosOpenMutexSem(PSZ name, PHMTX output)
{
    TRACE_NATIVE("DosOpenMutexSem('%s', %p)", name, output);
    return openSemaphore(name, LX_HANDLE_MUTEX, output);
}

// !!! FIXME: this is obviously not correct.
APIRET DosSetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosSetExceptionHandler(%p)", rec);
    return NO_ERROR;
} // DosSetExceptionHandler

// !!! FIXME: this is obviously not correct.
APIRET DosUnsetExceptionHandler(PEXCEPTIONREGISTRATIONRECORD rec)
{
    TRACE_NATIVE("DosUnsetExceptionHandler(%p)", rec);
    return NO_ERROR;
} // DosSetExceptionHandler

#if !LX_LEGACY
ULONG DosFlatToSel(VOID) { return 0; }
#else
ULONG _DosFlatToSel(PVOID ptr)
{
    TRACE_NATIVE("DosFlatToSel(%p)", ptr);
    return GLoaderState.convert32to1616(ptr);
} // _DosFlatToSel


#endif

APIRET DosSetSignalExceptionFocus(BOOL32 flag, PULONG pulTimes)
{
    TRACE_NATIVE("DosSetSignalExceptionFocus(%u, %p)", (uint) flag, pulTimes);

    if (flag == 0) {
        if (GLoaderState.main_module->signal_exception_focus_count == 0)
            return ERROR_ALREADY_RESET;
        GLoaderState.main_module->signal_exception_focus_count--;
    } else if (flag == 1) {
        if (GLoaderState.main_module->signal_exception_focus_count == 0xFFFFFFFF)
            return ERROR_NESTING_TOO_DEEP;
        GLoaderState.main_module->signal_exception_focus_count++;
    } else {
        // !!! FIXME: does OS/2 do something if flag != 0 or 1?
    } // else

    if (pulTimes)
        *pulTimes = GLoaderState.main_module->signal_exception_focus_count;

    // !!! FIXME: I guess enable/disable SIGINT handler here?

    return NO_ERROR;
} // DosSetSignalExceptionFocus

APIRET DosSetRelMaxFH(PLONG request, PULONG current)
{
    grabLock(&GMutexDosCalls);
    if (request && *request > 0) {
        uint32 increase = (uint32) *request;
        if (increase > 0x10000u - MaxHFiles) increase = 0x10000u - MaxHFiles;
        HFileInfo *files = increase ? realloc(HFiles, sizeof *files * (MaxHFiles + increase)) : NULL;
        if (files) {
            memset(files + MaxHFiles, 0, sizeof *files * increase);
            HFiles = files; MaxHFiles += increase;
        } else increase = 0;
        *request = (LONG) increase;
    }
    if (current) *current = MaxHFiles;
    ungrabLock(&GMutexDosCalls); return NO_ERROR;
}

APIRET DosAllocMem(PPVOID ppb, ULONG cb, ULONG flag)
{
    TRACE_NATIVE("DosAllocMem(%p, %u, %u)", ppb, (uint) cb, (uint) flag);

    if ((!ppb) || (cb == 0))
        return ERROR_INVALID_PARAMETER;

    const size_t pagesize = 4096;
    size_t size = (size_t) cb;
    if ((size % pagesize) != 0)
        size += pagesize - (size % pagesize);

    LxGuestMapping *mapping = NULL;
    void *addr;
    if (GLoaderState.execution) {
        mapping = lx_execution_map(GLoaderState.execution, 0, size, 3 | ((flag & 4) ? 4 : 0), 0);
        if (!mapping) return ERROR_NOT_ENOUGH_MEMORY;
        addr = lx_execution_mapping_host(mapping);
    } else {
        addr = lx_host_memory_alloc(size);
        if (!addr) return ERROR_NOT_ENOUGH_MEMORY;
    }

    SharedMemBlock *block = (SharedMemBlock *) malloc(sizeof (*block));
    if (!block) {
        if (mapping) lx_execution_unmap(GLoaderState.execution, mapping);
        else lx_host_memory_free(addr, size);
        return ERROR_NOT_ENOUGH_MEMORY;
    } // if

    block->addr = addr;
    block->size = size;
    block->mapping = mapping;
    block->freed = 0;

    grabLock(&GMutexDosCalls);
    block->next = GSharedMem;
    GSharedMem = block;
    ungrabLock(&GMutexDosCalls);

    *ppb = addr;
    TRACE_RET("DosAllocMem(%u) -> %p", (uint) cb, addr);
    return NO_ERROR;
} // DosAllocMem


APIRET DosSubSetMem(PVOID pbBase, ULONG flag, ULONG cb)
{
    TRACE_NATIVE("DosSubSetMem(%p, %u, %u)", pbBase, (uint) flag, (uint) cb);
    TRACE_RET("DosSubSetMem(%p, %u, %u) -> 0", pbBase, (uint) flag, (uint) cb);
    return NO_ERROR;  // !!! FIXME: obviously wrong
} // DosSubSetMem


APIRET DosSubAllocMem(PVOID pbBase, PPVOID ppb, ULONG cb)
{
    TRACE_NATIVE("DosSubAllocMem(%p, %p, %u)", pbBase, ppb, (uint) cb);
    *ppb = malloc(cb);  // !!! FIXME: obviously wrong
    if (!*ppb) {
        TRACE_RET("DosSubAllocMem(%p, %u) -> ENOMEM", pbBase, (uint) cb);
        return ERROR_NOT_ENOUGH_MEMORY;
    } // if
    TRACE_RET("DosSubAllocMem(%p, %u) -> %p", pbBase, (uint) cb, *ppb);
    return NO_ERROR;
} // DosSubAllocMem

static APIRET DosQueryHType_implementation(HFILE hFile, PULONG pType, PULONG pAttr)
{
    APIRET retval = NO_ERROR;

    grabLock(&GMutexDosCalls);
    if ((hFile >= MaxHFiles) || (HFiles[hFile].handle <= 1))
        retval = ERROR_INVALID_HANDLE;
    else {
        // OS/2 will dereference a NULL pointer here, but I can't do it...!!!
        if (pType) *pType = HFiles[hFile].type;
        if (pAttr) *pAttr = HFiles[hFile].attr;
    } // else
    ungrabLock(&GMutexDosCalls);

    return retval;
} // DosQueryHType_implementation

APIRET DosQueryHType(HFILE hFile, PULONG pType, PULONG pAttr)
{
    TRACE_NATIVE("DosQueryHType(%u, %p, %p)", (uint) hFile, pType, pAttr);
    return DosQueryHType_implementation(hFile, pType, pAttr);
} // DosQueryHType

APIRET DosSetMem(PVOID pb, ULONG cb, ULONG flag)
{
    TRACE_NATIVE("DosSetMem(%p, %u, %u)", pb, (uint) cb, (uint) flag);
    return NO_ERROR;  // !!! FIXME: obviously wrong.
} // DosSetMem

APIRET DosGetDateTime(PDATETIME pdt)
{
    TRACE_NATIVE("DosGetDateTime(%p)", pdt);

    const uint64 now = lx_host_realtime_us();
    LxHostCalendar value;
    if (!pdt || !lx_host_calendar((int64_t) (now / 1000000), 0, &value)) return ERROR_INVALID_PARAMETER;
    pdt->hours = value.hour; pdt->minutes = value.minute;
    pdt->seconds = value.second > 59 ? 59 : value.second;
    pdt->hundredths = (now % 1000000) / 10000;
    pdt->day = value.day; pdt->month = value.month; pdt->year = value.year;
    pdt->timezone = -value.utc_offset_minutes; pdt->weekday = value.weekday;

    return NO_ERROR;  // never returns failure.
} // DosGetDateTime

static char *makeUnixPath(const char *os2path, APIRET *err)
{
    return GLoaderState.makeUnixPath(os2path, (uint32 *) err);
} // makeUnixPath

static APIRET doDosOpen(PSZ name, PHFILE output, PULONG action, LONGLONG size, ULONG attributes,
                         ULONG open_flags, ULONG open_mode, PEAOP2 extended)
{
    if (!name || !output || size < 0 || extended || (attributes & FILE_DIRECTORY) ||
        (open_flags & 0xfffffe00u) || (open_mode & 0xffff0000u)) return ERROR_INVALID_PARAMETER;
    if (open_flags & OPEN_FLAGS_DASD) return ERROR_OPEN_FAILED;
    unsigned flags;
    switch (open_mode & 3) {
        case OPEN_ACCESS_READONLY: flags = LX_HOST_FILE_READ; break;
        case OPEN_ACCESS_WRITEONLY: flags = LX_HOST_FILE_WRITE; break;
        case OPEN_ACCESS_READWRITE: flags = LX_HOST_FILE_READ | LX_HOST_FILE_WRITE; break;
        default: return ERROR_INVALID_PARAMETER;
    }
    const ULONG existing = open_flags & 15, missing = open_flags & 0xf0;
    if (existing > OPEN_ACTION_REPLACE_IF_EXISTS || (missing != OPEN_ACTION_FAIL_IF_NEW && missing != OPEN_ACTION_CREATE_IF_NEW))
        return ERROR_INVALID_PARAMETER;
    if (!(flags & LX_HOST_FILE_WRITE) && (size || missing == OPEN_ACTION_CREATE_IF_NEW || existing == OPEN_ACTION_REPLACE_IF_EXISTS))
        return ERROR_INVALID_PARAMETER;
    if (missing == OPEN_ACTION_CREATE_IF_NEW || existing == OPEN_ACTION_FAIL_IF_EXISTS)
        flags |= LX_HOST_FILE_CREATE | LX_HOST_FILE_EXCLUSIVE;
    if (existing == OPEN_ACTION_REPLACE_IF_EXISTS) flags |= LX_HOST_FILE_TRUNCATE;
    if (open_mode & OPEN_FLAGS_WRITE_THROUGH) flags |= LX_HOST_FILE_SYNC;
    if (open_mode & OPEN_FLAGS_NOINHERIT) flags |= LX_HOST_FILE_NOINHERIT;
    APIRET result;
    char *path = makeUnixPath(name, &result); if (!path) return result;
    grabLock(&GMutexDosCalls);
    HFILE id;
    for (id = 0; id < MaxHFiles && HFiles[id].handle; id++) {}
    if (id == MaxHFiles) { ungrabLock(&GMutexDosCalls); free(path); return ERROR_TOO_MANY_OPEN_FILES; }
    HFiles[id].handle = 1; ungrabLock(&GMutexDosCalls);
    LxHostFsError error;
    LxHostFile *file = lx_host_file_open(path, flags, (attributes & FILE_READONLY) != 0, &error);
    int created = file && (flags & LX_HOST_FILE_EXCLUSIVE);
    if (!file && error == LX_HOST_FS_EXISTS && missing == OPEN_ACTION_CREATE_IF_NEW && existing != OPEN_ACTION_FAIL_IF_EXISTS)
        file = lx_host_file_open(path, flags & ~LX_HOST_FILE_EXCLUSIVE, (attributes & FILE_READONLY) != 0, &error);
    free(path);
    if (file && size && (created || existing == OPEN_ACTION_REPLACE_IF_EXISTS)) {
        error = lx_host_file_resize(file, (uint64) size);
        if (error) { lx_host_file_close(file); file = NULL; }
    }
    HFileInfo info;
    result = file ? (initFileInfo(file, &info, open_mode) ? NO_ERROR : ERROR_NOT_ENOUGH_MEMORY) : fileError(error);
    grabLock(&GMutexDosCalls);
    if (result) memset(&HFiles[id], 0, sizeof HFiles[id]); else HFiles[id] = info;
    ungrabLock(&GMutexDosCalls);
    if (!result) {
        *output = id;
        if (action) *action = created ? FILE_CREATED : existing == OPEN_ACTION_REPLACE_IF_EXISTS ? FILE_TRUNCATED : FILE_EXISTED;
    }
    return result;
}

APIRET DosOpen(PSZ pszFileName, PHFILE pHf, PULONG pulAction, ULONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosOpen('%s', %p, %p, %u, %u, %u, %u, %p)", pszFileName, pHf, pulAction, (uint) cbFile, (uint) ulAttribute, (uint) fsOpenFlags, (uint) fsOpenMode, peaop2);
    const APIRET rc = doDosOpen(pszFileName, pHf, pulAction, (LONGLONG) cbFile, ulAttribute, fsOpenFlags, fsOpenMode, peaop2);
    TRACE_RET("DosOpen('%s') -> %u handle=%u action=%u", pszFileName, (uint) rc,
              (uint) (pHf ? *pHf : 0), (uint) (pulAction ? *pulAction : 0));
    return rc;
} // DosOpen

static APIRET DosRequestMutexSemResource(HMTX hmtx, ULONG ulTimeout, LxHostMutex *mutex)
{
    TRACE_NATIVE("DosRequestMutexSem(%u, %u)", (uint) hmtx, (uint) ulTimeout);

    if (!hmtx)
        return ERROR_INVALID_HANDLE;

    switch (lx_host_mutex_lock(mutex, ulTimeout)) {
        case LX_HOST_LOCK_OK: return NO_ERROR;
        case LX_HOST_LOCK_BUSY:
        case LX_HOST_LOCK_TIMEOUT: return ERROR_TIMEOUT;
        case LX_HOST_LOCK_OWNER_DIED: return ERROR_SEM_OWNER_DIED;
        default: return ERROR_INVALID_HANDLE;
    }

}
APIRET DosRequestMutexSem(HMTX hmtx, ULONG ulTimeout)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hmtx, LX_HANDLE_MUTEX, &object)) return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_REQUEST_MUTEX, hmtx, ulTimeout, &object, NULL, NULL);
    return result;
}
 // DosRequestMutexSem

static APIRET DosReleaseMutexSemResource(HMTX hmtx, LxHostMutex *mutex)
{
    TRACE_NATIVE("DosReleaseMutexSem(%u)", (uint) hmtx);

    if (!hmtx)
        return ERROR_INVALID_HANDLE;

    const LxHostLockResult rc = lx_host_mutex_unlock(mutex);
    if (rc == LX_HOST_LOCK_OK) return NO_ERROR;
    if (rc == LX_HOST_LOCK_NOT_OWNER) return ERROR_NOT_OWNER;
    return ERROR_INVALID_HANDLE;

}
APIRET DosReleaseMutexSem(HMTX hmtx)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hmtx, LX_HANDLE_MUTEX, &object)) return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_RELEASE_MUTEX, hmtx, 0, &object, NULL, NULL);
    return result;
}
 // DosReleaseMutexSem

static APIRET DosSetFilePtr_implementation(HFILE id, LONG distance, ULONG origin, PULONG output)
{
    if (origin > FILE_END) return ERROR_INVALID_FUNCTION;
    LxHandleBorrow file;
    if (!borrowFile(id, &file)) return ERROR_INVALID_HANDLE;
    uint64 position;
    const LxHostFsError error = lx_host_file_seek(file.resource, distance, origin, &position);
    lx_handle_release(&file);
    if (!error && output) *output = (ULONG) position;
    return error == LX_HOST_FS_INVALID ? ERROR_NEGATIVE_SEEK : fileError(error);
}

APIRET DosSetFilePtr(HFILE hFile, LONG ib, ULONG method, PULONG ibActual)
{
    TRACE_NATIVE("DosSetFilePtr(%u, %d, %u, %p)", (uint) hFile, (int) ib, (uint) method, ibActual);
    return DosSetFilePtr_implementation(hFile, ib, method, ibActual);
} // DosSetFilePtr

static APIRET DosRead_implementation(HFILE id, PVOID data, ULONG size, PULONG actual)
{
    LxHandleBorrow file;
    if (!borrowFile(id, &file)) return ERROR_INVALID_HANDLE;
    size_t count = 0; LxHostFsError error;
    error = (LxHostFsError) guardedResource(RESOURCE_READ, id, size, &file, data, &count);
    if (actual) *actual = (ULONG) count;
    return fileError(error);
}

APIRET DosRead(HFILE hFile, PVOID pBuffer, ULONG cbRead, PULONG pcbActual)
{
    TRACE_NATIVE("DosRead(%u, %p, %u, %p)", (uint) hFile, pBuffer, (uint) cbRead, pcbActual);
    return DosRead_implementation(hFile, pBuffer, cbRead, pcbActual);
} // DosRead

static APIRET DosClose_implementation(HFILE id)
{
    grabLock(&GMutexDosCalls);
    APIRET result = ERROR_INVALID_HANDLE;
    if (id < MaxHFiles && HFiles[id].handle > 1) {
        uint32 handle = HFiles[id].handle; memset(&HFiles[id], 0, sizeof HFiles[id]);
        result = lx_handle_close(handle, LX_HANDLE_FILE) ? NO_ERROR : ERROR_INVALID_HANDLE;
    }
    ungrabLock(&GMutexDosCalls); return result;
}

APIRET DosClose(HFILE hFile)
{
    TRACE_NATIVE("DosClose(%u)", (uint) hFile);
    return DosClose_implementation(hFile);
} // DosClose


APIRET DosEnterMustComplete(PULONG pulNesting)
{
    TRACE_NATIVE("DosEnterMustComplete(%p)", pulNesting);

    PTIB2 tib2 = (PTIB2) LX_GETTIB2();
    if (tib2->tib2_usMCCount == 0xFFFF) {
        return ERROR_NESTING_TOO_DEEP;
    } else if (tib2->tib2_usMCCount == 0) {
        FIXME("block signals here");
    }

    tib2->tib2_usMCCount++;

    if (pulNesting)
        *pulNesting = tib2->tib2_usMCCount;

    return NO_ERROR;
} // DosEnterMustComplete

APIRET DosExitMustComplete(PULONG pulNesting)
{
    TRACE_NATIVE("DosExitMustComplete(%p)", pulNesting);

    PTIB2 tib2 = (PTIB2) LX_GETTIB2();
    if (tib2->tib2_usMCCount == 0) {
        return ERROR_ALREADY_RESET;
    } else if (tib2->tib2_usMCCount == 1) {
        FIXME("unblock signals here");
    }

    tib2->tib2_usMCCount--;

    if (pulNesting)
        *pulNesting = tib2->tib2_usMCCount;

    return NO_ERROR;
} // DosExitMustComplete

static void unixTimeToOs2(const int64_t seconds, FDATE *date, FTIME *time)
{
    LxHostCalendar value;
    if (!lx_host_calendar(seconds, 0, &value)) { memset(date, 0, sizeof *date); memset(time, 0, sizeof *time); return; }
    date->day = value.day; date->month = value.month;
    date->year = value.year < 1980 ? 0 : value.year > 2107 ? 127 : value.year - 1980;
    time->twosecs = value.second / 2; time->minutes = value.minute; time->hours = value.hour;
} // unixTimeToOs2

static inline void initFileCreationDateTime(FDATE *fdate, FTIME *ftime)
{
    // we don't store creation date on Unix. OS/2 zeroes out fields that aren't
    //  appropriate to FAT file systems, but I don't want to risk apps using
    //  zeroes to decide a file in on a FAT disk and reducing features,
    //  shrinking filenames to 8.3, etc.
    if (fdate) {
        fdate->day = 1;
        fdate->month = 1;
        fdate->year = 0;  // years since 1980, apparently.
    } // if

    if (ftime) {
        ftime->twosecs = 0;
        ftime->minutes = 0;
        ftime->hours = 0;
    } // if
} // initFileCreationDateTime

static APIRET queryFileInfoStandardFromStat(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FILESTATUS3 *st = (FILESTATUS3 *) pInfoBuf;
    memset(st, '\0', sizeof (*st));

    initFileCreationDateTime(&st->fdateCreation, &st->ftimeCreation);
    unixTimeToOs2(statbuf->accessed, &st->fdateLastAccess, &st->ftimeLastAccess);
    unixTimeToOs2(statbuf->modified, &st->fdateLastWrite, &st->ftimeLastWrite);

    st->cbFile = (ULONG) statbuf->size;
    st->cbFileAlloc = (ULONG) statbuf->allocated;

    if (statbuf->kind == LX_HOST_FILE_DIRECTORY)
        st->attrFile |= FILE_DIRECTORY;
    if (statbuf->readonly)  // !!! FIXME: not accurate...?
        st->attrFile |= FILE_READONLY;

    return NO_ERROR;
} // queryFileInfoStandardFromStat

static APIRET queryPathInfoStandard(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof (FILESTATUS3))
        return ERROR_BUFFER_OVERFLOW;

    LxHostFileInfo statbuf;
    if (lx_host_path_info(unixPath, &statbuf) != LX_HOST_FS_OK) {
        return ERROR_PATH_NOT_FOUND;  // !!! FIXME
    }

    return queryFileInfoStandardFromStat(&statbuf, pInfoBuf, cbInfoBuf);
} // queryPathInfoStandard

static APIRET queryPathInfoEaSize(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof (FILESTATUS4))
        return ERROR_BUFFER_OVERFLOW;
    const APIRET rc = queryPathInfoStandard(unixPath, pInfoBuf, cbInfoBuf);
    if (rc != NO_ERROR)
        return rc;

    FILESTATUS4 *st = (FILESTATUS4 *) pInfoBuf;
    FIXME("write me");
    st->cbList = 0;
    return NO_ERROR;
} // queryPathInfoEaSize

static APIRET queryPathInfoFullName(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    char *real = lx_host_path_canonical(unixPath);
    if (real == NULL) {
        // !!! FIXME: what _should_ this do if file doesn't exist? If parent directory doesn't?
        real = strdup(unixPath);  // could be file doesn't exist, but this still needs to return something...
        if (real == NULL) {
            return ERROR_NOT_ENOUGH_MEMORY;  // !!! FIXME: no
        }
    }

    if ((strlen(real) + 3) >= cbInfoBuf) {
        free(real);
        return ERROR_BUFFER_OVERFLOW;
    }

    // !!! FIXME: this needs to map to drive letters defined in a config
    // !!! FIXME:  eventually.
    char *dst = (char *) pInfoBuf;
    *(dst++) = 'C';
    *(dst++) = ':';
    strcpy(dst, real);
    free(real);

    while (*dst) {
        if (*dst == '/')
            *dst = '\\';
        dst++;
    }

    return NO_ERROR;
} // queryPathInfoFullName

static APIRET queryPathInfoEasFromList(PSZ unixPath, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FIXME("write me");
    return ERROR_INVALID_LEVEL;
} // queryPathInfoEasFromList

static APIRET queryPathInfo(PSZ unixPath, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    switch (ulInfoLevel) {
        case FIL_STANDARD: return queryPathInfoStandard(unixPath, pInfoBuf, cbInfoBuf);
        case FIL_QUERYEASIZE: return queryPathInfoEaSize(unixPath, pInfoBuf, cbInfoBuf);
        case FIL_QUERYEASFROMLIST: return queryPathInfoEasFromList(unixPath, pInfoBuf, cbInfoBuf);

        // OS/2 has an undocumented info level, 7, that appears to return a case-corrected version
        //  of the path. (FIL_QUERYFULLNAME doesn't correct the case of what the app queries on
        //  OS/2). Since we have to case-correct for a Unix filesystem anyhow, we just use the
        //  usual FIL_QUERYFULLNAME to handle the undocumented level. Java uses this level.
        case 7:
        case FIL_QUERYFULLNAME: return queryPathInfoFullName(unixPath, pInfoBuf, cbInfoBuf);
        default: break;
    } // switch

    return ERROR_INVALID_LEVEL;
} // queryPathInfo

APIRET DosQueryPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    TRACE_NATIVE("DosQueryPathInfo('%s', %u, %p, %u)", pszPathName, (uint) ulInfoLevel, pInfoBuf, (uint) cbInfoBuf);

    APIRET err = NO_ERROR;
    char *unixPath = makeUnixPath(pszPathName, &err);
    if (!unixPath)
        return err;
    err = queryPathInfo(unixPath, ulInfoLevel, pInfoBuf, cbInfoBuf);
    TRACE_RET("DosQueryPathInfo('%s', level=%u, cb=%u) -> %u", pszPathName,
              (uint) ulInfoLevel, (uint) cbInfoBuf, (uint) err);
    free(unixPath);
    return err;
} // DosQueryPathInfo

static APIRET queryFileInfoStandard(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof (FILESTATUS3))
        return ERROR_BUFFER_OVERFLOW;
    return queryFileInfoStandardFromStat(statbuf, pInfoBuf, cbInfoBuf);
} // queryFileInfoStandard

static APIRET queryFileInfoEaSize(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    if (cbInfoBuf < sizeof (FILESTATUS4))
        return ERROR_BUFFER_OVERFLOW;
    const APIRET rc = queryFileInfoStandard(statbuf, pInfoBuf, cbInfoBuf);
    if (rc != NO_ERROR)
        return rc;

    FILESTATUS4 *st = (FILESTATUS4 *) pInfoBuf;
    FIXME("write me");
    st->cbList = 0;
    return NO_ERROR;
} // queryFileInfoEaSize

static APIRET queryFileInfoEasFromList(const LxHostFileInfo *statbuf, PVOID pInfoBuf, ULONG cbInfoBuf)
{
    FIXME("write me");
    return ERROR_INVALID_LEVEL;
} // queryFileInfoEasFromList


APIRET DosQueryFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo, ULONG cbInfoBuf)
{
    TRACE_NATIVE("DosQueryFileInfo(%u, %u, %p, %u)", (uint) hf, (uint) ulInfoLevel, pInfo, (uint) cbInfoBuf);

    LxHandleBorrow file;
    if (!borrowFile(hf, &file)) return ERROR_INVALID_HANDLE;
    LxHostFileInfo statbuf;
    LxHostFsError error = lx_host_file_info(file.resource, &statbuf);
    lx_handle_release(&file);
    if (error) return fileError(error);

    APIRET rc;
    switch (ulInfoLevel) {
        case FIL_STANDARD: rc = queryFileInfoStandard(&statbuf, pInfo, cbInfoBuf); break;
        case FIL_QUERYEASIZE: rc = queryFileInfoEaSize(&statbuf, pInfo, cbInfoBuf); break;
        case FIL_QUERYEASFROMLIST: rc = queryFileInfoEasFromList(&statbuf, pInfo, cbInfoBuf); break;
        default: rc = ERROR_INVALID_LEVEL; break;
    } // switch

    TRACE_RET("DosQueryFileInfo(%u, level=%u, cb=%u) -> %u size=%ld", (uint) hf,
              (uint) ulInfoLevel, (uint) cbInfoBuf, (uint) rc,
              (long) statbuf.size);
    return rc;
} // DosQueryFileInfo

static void destroyThreadResource(void *value)
{
    Thread *thread = value;
    lx_execution_destroy_thread32(GLoaderState.execution, thread->invocation);
    if (thread->stack) {
        lx_execution_stack_segments(GLoaderState.execution, lx_execution_mapping_address(thread->stack),
                                     lx_execution_mapping_size(thread->stack), 1);
        lx_execution_unmap(GLoaderState.execution, thread->stack);
    }
    free(thread);
}
static void os2ThreadCleanup(void *arg)
{
    Thread *thread = arg;
    GLoaderState.deinitOs2Tib(thread->selector);
    grabLock(&GMutexDosCalls); thread->finished = 1; ungrabLock(&GMutexDosCalls);
}
static void *os2ThreadEntry(void *arg)
{
    Thread *thread = arg;
    uint8 tibspace[LXTIBSIZE] = {0};
    const size_t size = lx_execution_mapping_size(thread->stack);
    GLoaderState.initOs2Tib(tibspace, (uint8 *) lx_execution_mapping_host(thread->stack) + size, size, thread->id);
    thread->selector = GLoaderState.setOs2Tib(tibspace);
    lx_execution_thread32(GLoaderState.execution, thread->invocation);
    return NULL;
}
APIRET DosCreateThread(PTID ptid, PFNTHREAD entry, ULONG argument, ULONG flags, ULONG bytes)
{
    TRACE_NATIVE("DosCreateThread(%p, %p, %u, %u, %u)", ptid, entry, (uint) argument, (uint) flags, (uint) bytes);
    if (!ptid || !entry || (flags & CREATE_SUSPENDED) || bytes > 0x1000000) return ERROR_INVALID_PARAMETER;
    const uint32 size = bytes ? (bytes + 0xffff) & ~0xffffu : 0x10000;
    Thread *thread = calloc(1, sizeof *thread);
    if (!thread) return ERROR_NOT_ENOUGH_MEMORY;
    grabLock(&GMutexDosCalls);
    if (GNextThreadId > UINT16_MAX) { ungrabLock(&GMutexDosCalls); free(thread); return ERROR_MAX_THRDS_REACHED; }
    while (GNextThreadStack <= 0x1fff0000u - size) {
        uint32 base = GNextThreadStack; GNextThreadStack += size + 0x10000;
        thread->stack = lx_execution_map(GLoaderState.execution, base, size, 3, 1);
        if (thread->stack) break;
    }
    if (!thread->stack) { ungrabLock(&GMutexDosCalls); free(thread); return ERROR_NOT_ENOUGH_MEMORY; }
    lx_execution_stack_segments(GLoaderState.execution, lx_execution_mapping_address(thread->stack), size, 0);
    thread->invocation = lx_execution_prepare_thread32(GLoaderState.execution, (uint32) (uintptr_t) entry,
        lx_execution_mapping_address(thread->stack) + size - 16, argument);
    thread->id = GNextThreadId++;
    if (thread->invocation) thread->handle = lx_handle_create(LX_HANDLE_THREAD, thread, destroyThreadResource);
    if (!thread->handle) {
        ungrabLock(&GMutexDosCalls); destroyThreadResource(thread); return ERROR_NOT_ENOUGH_MEMORY;
    }
    thread->thread = lx_host_thread_create(os2ThreadEntry, thread, os2ThreadCleanup);
    if (!thread->thread) {
        ungrabLock(&GMutexDosCalls); lx_handle_close(thread->handle, LX_HANDLE_THREAD); return ERROR_MAX_THRDS_REACHED;
    }
    thread->next = GThreads; GThreads = thread; *ptid = thread->id;
    /* Process exit must not unload code beneath any guest thread. */
    __atomic_store_n(&GLoaderState.ne_threads_started, 1, __ATOMIC_RELEASE);
    ungrabLock(&GMutexDosCalls); return NO_ERROR;
}

static char *findExePath(const char *exe, APIRET *err)
{
    FIXME("eventually this needs to search the path and maybe launch native (non-OS/2) processes too");
    char *retval = lx_host_executable_path();
    if (!retval)
        *err = ERROR_NOT_ENOUGH_MEMORY;
    return retval;
} // findExePath

static char **calcEnvp(const char *os2env, const char *exe, APIRET *err)
{
    // Build a NULL-terminated environment for the child lx_loader. Besides
    //  the caller-supplied OS/2 environment, keep the host settings the
    //  loader itself needs to find lib2ine/libdoscalls and the OS/2 DLLs.
    size_t count = 0;
    const char *src = os2env;
    if (!src) {
        extern char **environ;
        while (environ[count])
            count++;
    } else {
        while (*src) {
            count++;
            src += strlen(src) + 1;
        } // while
    } // else

    const char *tlib = getenv("TWOINE_LIBPATH");
    const char *ldlib = getenv("LD_LIBRARY_PATH");
    static const char *passthru[] = {
        "TRACE_NATIVE", "TWOINE_TRACE_RET", "TWOINE_TRACE_SEGMENTS",
        "TWOINE_KEEP_TEMPS", "TWOINE_TRACE_DOSINT", "TWOINE_BACKEND", NULL
    };
    char **retval = (char **) calloc(count + 16, sizeof (char *));
    if (!retval) {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    } // if

    size_t idx = 0;
    if (!os2env) {
        extern char **environ;
        for (size_t i = 0; i < count; i++)
            retval[idx++] = strdup(environ[i]);
    } else {
        src = os2env;
        while (*src) {
            retval[idx++] = strdup(src);
            src += strlen(src) + 1;
        } // while
    } // else

    char buf[4096];
    snprintf(buf, sizeof (buf), "IS_2INE=%s", exe);
    retval[idx++] = strdup(buf);
    if (tlib) {
        snprintf(buf, sizeof (buf), "TWOINE_LIBPATH=%s", tlib);
        retval[idx++] = strdup(buf);
    } // if
    if (ldlib) {
        snprintf(buf, sizeof (buf), "LD_LIBRARY_PATH=%s", ldlib);
        retval[idx++] = strdup(buf);
    } // if
    for (int i = 0; passthru[i]; i++) {
        const char *val = getenv(passthru[i]);
        if (val) {
            snprintf(buf, sizeof (buf), "%s=%s", passthru[i], val);
            retval[idx++] = strdup(buf);
        } // if
    } // for
    retval[idx] = NULL;
    return retval;
} // calcEnvp

static char **calcArgv(const char *os2args, const char *progpath, APIRET *err)
{
    if ((!os2args) || (!*os2args))
        os2args = "\0";

    // OS/2 passes one string for argv[0], a NUL, then the argument string.
    //  The child here is another invocation of lx_loader, so its argv[0] is
    //  the loader name and argv[1] is the OS/2 program to load. Everything
    //  after the first NUL becomes the child's arguments.
    const char *args = os2args + strlen(os2args) + 1;

    size_t maxargs = 3;
    for (const char *ptr = args; *ptr; ptr++) {
        if ((*ptr == ' ') || (*ptr == '\t'))
            maxargs++;
    } // for

    const size_t bufsize = 16 + strlen(progpath) + 1 + strlen(args) + 1;
    char **retval = (char **) malloc((maxargs + 1) * sizeof (char *) + bufsize);
    if (!retval) {
        *err = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    } // if

    char *dst = (char *) (retval + maxargs + 1);
    size_t idx = 0;

    strcpy(dst, "lx_loader");
    retval[idx++] = dst;
    dst += strlen(dst) + 1;

    strcpy(dst, progpath);
    retval[idx++] = dst;
    dst += strlen(dst) + 1;

    char *start = dst;
    int in_quotes = 0;
    int have_token = 0;

    for (const char *ptr = args; ; ptr++) {
        const char ch = *ptr;
        if (ch == '"') {
            in_quotes = !in_quotes;
            have_token = 1;
            continue;
        } else if ((ch == '\0') || ((!in_quotes) && ((ch == ' ') || (ch == '\t')))) {
            if (have_token) {
                *(dst++) = '\0';
                retval[idx++] = start;
                start = dst;
                have_token = 0;
            } // if
            if (ch == '\0')
                break;
            continue;
        } // else if

        if (!have_token) {
            start = dst;
            have_token = 1;
        } // if
        *(dst++) = ch;
    } // for

    retval[idx] = NULL;
    return retval;
} // calcArgv

typedef struct ChildProcess {
    LxHostChild *host;
    int waiting;
    struct ChildProcess *next;
} ChildProcess;
static ChildProcess *GChildren;
static void setProcessResultCode(PRESULTCODES output, const LxHostChildStatus *status)
{
    if (!output) return;
    const ULONG reasons[] = {TC_EXIT, TC_KILLPROCESS, TC_HARDERROR, TC_EXCEPTION};
    output->codeTerminate = reasons[status->reason]; output->codeResult = status->result;
}

static APIRET DosExecPgm_implementation(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg, PSZ pEnv, PRESULTCODES pRes, PSZ pName)
{
    APIRET retval = NO_ERROR;

    if (cbObjname < 0) return ERROR_INVALID_PARAMETER;
    if (pObjname && cbObjname > 0)
        *pObjname = '\0';

    if ((execFlag == EXEC_TRACE) || (execFlag == EXEC_ASYNCRESULTDB)) { // sorry debuggers!
        FIXME("tried to launch a process for debugging!");
        return ERROR_ACCESS_DENIED;
    } // if

    if (execFlag == EXEC_LOAD) {
        FIXME("What is this, exactly?");
        return ERROR_GEN_FAILURE;
    } // if

    APIRET err;
    char *pNameUnix = makeUnixPath(pName, &err);
    if (!pNameUnix)
        return err;

    // calculate the args and environment first, so the child process is literally just calling execve().
    char *exe = findExePath(pName, &retval);
    if (!exe) {
        free(pNameUnix);
        return retval;
    } // if

    char **argv = calcArgv(pArg, pName, &retval);
    if (!argv) {
        free(exe);
        free(pNameUnix);
        return retval;
    } // if

    char **envp = calcEnvp(pEnv, pNameUnix, &retval);
    if (!envp) {
        free(argv);
        free(exe);
        free(pNameUnix);
        return retval;
    } // if

    ChildProcess *tracked = execFlag == EXEC_ASYNCRESULT ? calloc(1, sizeof *tracked) : NULL;
    if (execFlag != EXEC_SYNC && execFlag != EXEC_ASYNC && execFlag != EXEC_ASYNCRESULT && execFlag != EXEC_BACKGROUND)
        retval = ERROR_INVALID_FUNCTION;
    else if (execFlag == EXEC_ASYNCRESULT && !tracked) retval = ERROR_NOT_ENOUGH_MEMORY;
    else {
        LxHostChild *child = lx_host_child_spawn(exe, argv, envp);
        if (!child) retval = ERROR_NO_PROC_SLOTS;
        else if (execFlag == EXEC_SYNC) {
            LxHostChildStatus status;
            if (lx_host_child_wait(child, 0, &status) != 1) retval = ERROR_GEN_FAILURE;
            else setProcessResultCode(pRes, &status);
            lx_host_child_release(child);
        } else {
            if (pRes) { pRes->codeTerminate = lx_host_child_id(child); pRes->codeResult = 0; }
            if (tracked) {
                tracked->host = child;
                grabLock(&GMutexDosCalls); tracked->next = GChildren; GChildren = tracked; ungrabLock(&GMutexDosCalls);
                tracked = NULL;
            } else lx_host_child_release(child);
        }
    }
    free(tracked);
    for (char **item = envp; *item; item++) free(*item);
    free(pNameUnix);
    free(envp);
    free(argv);
    free(exe);

    return retval;
} // DosExecPgm_implementation

APIRET DosExecPgm(PCHAR pObjname, LONG cbObjname, ULONG execFlag, PSZ pArg, PSZ pEnv, PRESULTCODES pRes, PSZ pName)
{
    TRACE_NATIVE("DosExecPgm('%s', %u, %u, '%s', '%s', %p, '%s')", pObjname, (uint) cbObjname, (uint) execFlag, pArg, pEnv, pRes, pName);
    if (GLoaderState.trace_native && pArg) {
        fprintf(stderr, "2INE TRACE: DosExecPgm arg block:");
        for (const char *p = pArg; p < (pArg + 1024); p++) {
            if (*p) fprintf(stderr, " %c", (*p >= 32 && *p < 127) ? *p : '.');
            else { fprintf(stderr, " |"); if (p[1] == '\0') break; }
        }
        fprintf(stderr, "\n");
    }
    return DosExecPgm_implementation(pObjname, cbObjname, execFlag, pArg, pEnv, pRes, pName);
} // DosExecPgm

static APIRET DosResetEventSemResource(HEV hev, PULONG pulPostCt, EventSem *sem)
{
    TRACE_NATIVE("DosResetEventSem(%u, %p)", (uint) hev, pulPostCt);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    APIRET retval = NO_ERROR;
    ULONG count = 0;

    lx_host_monitor_lock(sem->monitor);
    count = sem->posted;
    if (!sem->posted)
        retval = ERROR_ALREADY_RESET;
    else
        sem->posted = 0;
    lx_host_monitor_unlock(sem->monitor);

    if (pulPostCt)
        *pulPostCt = count;

    return retval;
}
APIRET DosResetEventSem(HEV hev, PULONG pulPostCt)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hev, LX_HANDLE_EVENT, &object)) return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_RESET_EVENT, hev, 0, &object, NULL, pulPostCt);
    return result;
}
 // DosResetEventSem

static APIRET DosPostEventSemResource(HEV hev, EventSem *sem)
{
    TRACE_NATIVE("DosPostEventSem(%u)", (uint) hev);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    APIRET retval = NO_ERROR;

    lx_host_monitor_lock(sem->monitor);
    if (sem->posted)
        retval = ERROR_ALREADY_POSTED;
    else {
        sem->posted = 1;
        lx_host_monitor_wake_all(sem->monitor);
    } // else
    lx_host_monitor_unlock(sem->monitor);

    return retval;
}
APIRET DosPostEventSem(HEV hev)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hev, LX_HANDLE_EVENT, &object)) return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_POST_EVENT, hev, 0, &object, NULL, NULL);
    return result;
}
 // DosPostEventSem

APIRET DosWaitNPipe(PSZ pszName, ULONG ulTimeOut)
{
    TRACE_NATIVE("DosWaitNPipe('%s', %u)", pszName ? pszName : "",
                 (uint) ulTimeOut);

    if ((!pszName) || (!*pszName))
        return ERROR_INVALID_PARAMETER;

    // 2ine has no named-pipe provider yet, so no instance can become
    //  available. Report the OS/2 "nothing connected" result immediately
    //  instead of blocking for a pipe that will never appear.
    return ERROR_PIPE_NOT_CONNECTED;
} // DosWaitNPipe

static APIRET DosWaitEventSemResource(HEV hev, ULONG timeout, EventSem *sem)
{
    TRACE_NATIVE("DosWaitEventSem(%u, %u)", (uint) hev, (uint) timeout);
    const uint64 deadline = timeout == UINT32_MAX ? UINT64_MAX : lx_host_monotonic_us() + (uint64) timeout * 1000;
    APIRET result = NO_ERROR;
    lx_host_monitor_lock(sem->monitor);
    while (!sem->posted) {
        const uint64 now = lx_host_monotonic_us();
        if (!timeout || now >= deadline) { result = ERROR_TIMEOUT; break; }
        uint32 wait = UINT32_MAX;
        if (deadline != UINT64_MAX) wait = (uint32) ((deadline - now + 999) / 1000);
        LxHostLockResult rc = lx_host_monitor_wait(sem->monitor, wait);
        if (rc == LX_HOST_LOCK_FAILED) { result = ERROR_INVALID_HANDLE; break; }
    }
    lx_host_monitor_unlock(sem->monitor); return result;
}
APIRET DosWaitEventSem(HEV hev, ULONG timeout)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hev, LX_HANDLE_EVENT, &object)) return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_WAIT_EVENT, hev, timeout, &object, NULL, NULL);
    return result;
}


static APIRET DosQueryEventSemResource(HEV hev, PULONG pulPostCt, EventSem *sem)
{
    TRACE_NATIVE("DosQueryEventSem(%u, %p)", (uint) hev, pulPostCt);

    if (!hev)
        return ERROR_INVALID_HANDLE;

    lx_host_monitor_lock(sem->monitor);
    const ULONG count = (ULONG) sem->posted;
    lx_host_monitor_unlock(sem->monitor);

    if (pulPostCt)
        *pulPostCt = count;

    return NO_ERROR;
}
APIRET DosQueryEventSem(HEV hev, PULONG pulPostCt)
{
    LxHandleBorrow object;
    if (!lx_handle_borrow(hev, LX_HANDLE_EVENT, &object)) return ERROR_INVALID_HANDLE;
    APIRET result;
    result = guardedResource(RESOURCE_QUERY_EVENT, hev, 0, &object, NULL, pulPostCt);
    return result;
}
 // DosQueryEventSem

APIRET DosCloseEventSem(HEV handle)
{
    TRACE_NATIVE("DosCloseEventSem(%u)", (uint) handle);
    LxHandleBorrow object;
    if (!lx_handle_borrow(handle, LX_HANDLE_EVENT, &object)) return ERROR_INVALID_HANDLE;
    grabLock(&GMutexDosCalls);
    int closed = lx_handle_close(handle, LX_HANDLE_EVENT);
    if (closed) retireSemaphoreName(object.resource, LX_HANDLE_EVENT);
    ungrabLock(&GMutexDosCalls);
    lx_handle_release(&object); return closed ? NO_ERROR : ERROR_INVALID_HANDLE;
}

static void makeSharedMemName(const char *os2name, char *buf, const size_t buflen)
{
    // OS/2 names are case-insensitive and use backslashes; fold to one
    //  collision-resistant POSIX name.
    uint64_t hash = 1469598103934665603ULL;  // FNV-1a 64-bit offset basis.
    for (const char *ptr = os2name; *ptr; ptr++) {
        unsigned char ch = (unsigned char) *ptr;
        if ((ch >= 'A') && (ch <= 'Z'))
            ch += 'a' - 'A';
        hash ^= ch;
        hash *= 1099511628211ULL;  // FNV-1a 64-bit prime.
    } // for
    snprintf(buf, buflen, "/2ine-shm-%016llx", (unsigned long long) hash);
} // makeSharedMemName

static APIRET mapNamedSharedMemory(PPVOID output, const char *name, ULONG size, ULONG flags)
{
    if (!output || !name || !*name || (flags & ~(PAG_READ | PAG_WRITE | PAG_COMMIT | OBJ_TILE | OBJ_GETTABLE | OBJ_GIVEABLE)))
        return ERROR_INVALID_PARAMETER;
    char identifier[128]; makeSharedMemName(name, identifier, sizeof identifier);
    LxHostShared *object = NULL;
    const LxHostSharedResult opened = lx_host_shared_open(identifier, size, &object);
    if (opened != LX_HOST_SHARED_OK) return opened == LX_HOST_SHARED_MISSING ? ERROR_FILE_NOT_FOUND :
        opened == LX_HOST_SHARED_EXISTS ? ERROR_DUPLICATE_NAME : ERROR_NOT_ENOUGH_MEMORY;
    LxGuestMapping *mapping = lx_execution_map_shared(GLoaderState.execution, object);
    lx_host_shared_close(object);
    if (!mapping) return ERROR_NOT_ENOUGH_MEMORY;
    SharedMemBlock *block = calloc(1, sizeof *block);
    if (!block) { lx_execution_unmap(GLoaderState.execution, mapping); return ERROR_NOT_ENOUGH_MEMORY; }
    block->mapping = mapping; block->addr = lx_execution_mapping_host(mapping);
    block->size = lx_execution_mapping_size(mapping);
    grabLock(&GMutexDosCalls); block->next = GSharedMem; GSharedMem = block; ungrabLock(&GMutexDosCalls);
    *output = block->addr; return NO_ERROR;
}
APIRET DosGetNamedSharedMem(PPVOID output, PSZ name, ULONG flags)
{
    TRACE_NATIVE("DosGetNamedSharedMem(%p, '%s', %u)", output, name ? name : "", (uint) flags);
    return mapNamedSharedMemory(output, name, 0, flags);
}
APIRET DosAllocSharedMem(PPVOID output, PSZ name, ULONG size, ULONG flags)
{
    TRACE_NATIVE("DosAllocSharedMem(%p, '%s', %u, %u)", output, name ? name : "", (uint) size, (uint) flags);
    if (!size || size > UINT32_MAX - 4095) return ERROR_INVALID_PARAMETER;
    if (!name || !*name) return DosAllocMem(output, size, flags);
    return mapNamedSharedMemory(output, name, (size + 4095) & ~4095u, flags);
}

APIRET DosGiveSharedMem(PVOID pb, PID pid, ULONG flag)
{
    TRACE_NATIVE("DosGiveSharedMem(%p, %u, %u)", pb, (uint) pid, (uint) flag);
    // Shared mappings are already visible to every process that can name
    //  them; there is no per-process grant to perform here.
    return NO_ERROR;
} // DosGiveSharedMem

APIRET DosFreeMem(PVOID pb)
{
    TRACE_NATIVE("DosFreeMem(%p)", pb);

    // !!! FIXME: this API is actually much more complicated than this.

    // Retain retired mappings until worker teardown. The IBM C runtime's
    // exit path still accesses its errno storage after freeing the block.
    grabLock(&GMutexDosCalls);
    for (SharedMemBlock *block = GSharedMem; block; block = block->next) {
        if (block->addr == pb) {
            block->freed = 1;
            ungrabLock(&GMutexDosCalls);
            return NO_ERROR;
        } // if
    } // for
    ungrabLock(&GMutexDosCalls);

    return ERROR_INVALID_ADDRESS;
} // DosFreeMem

APIRET DosWaitChild(ULONG action, ULONG option, PRESULTCODES result, PPID output_pid, PID pid)
{
    TRACE_NATIVE("DosWaitChild(%u, %u, %p, %p, %u)", (uint) action, (uint) option, result, output_pid, (uint) pid);
    if (action != DCWA_PROCESS || (option != DCWW_WAIT && option != DCWW_NOWAIT)) return ERROR_INVALID_PARAMETER;
    for (;;) {
        grabLock(&GMutexDosCalls);
        ChildProcess *child = NULL;
        LxHostChildStatus status; int ready = 0;
        for (ChildProcess *item = GChildren; item; item = item->next) {
            if (item->waiting || (pid && lx_host_child_id(item->host) != pid)) continue;
            if (pid) { child = item; break; }
            ready = lx_host_child_wait(item->host, 1, &status);
            if (ready) { child = item; break; }
        }
        if (!child) {
            int any = GChildren != NULL; ungrabLock(&GMutexDosCalls);
            if (pid || !any) return pid ? ERROR_INVALID_PROCID : ERROR_WAIT_NO_CHILDREN;
            if (option == DCWW_NOWAIT) return ERROR_CHILD_NOT_COMPLETE;
            lx_host_sleep_ms(1); continue;
        }
        child->waiting = 1; ungrabLock(&GMutexDosCalls);
        if (!ready) ready = lx_host_child_wait(child->host, option == DCWW_NOWAIT, &status);
        grabLock(&GMutexDosCalls);
        if (ready == 1) {
            for (ChildProcess **link = &GChildren; *link; link = &(*link)->next)
                if (*link == child) { *link = child->next; break; }
        } else child->waiting = 0;
        ungrabLock(&GMutexDosCalls);
        if (ready != 1) return ready ? ERROR_INVALID_PROCID : ERROR_CHILD_NOT_COMPLETE;
        if (output_pid) *output_pid = lx_host_child_id(child->host);
        setProcessResultCode(result, &status); lx_host_child_release(child->host); free(child); return NO_ERROR;
    }
}

APIRET DosWaitThread(PTID id, ULONG option)
{
    TRACE_NATIVE("DosWaitThread(%p, %u)", id, (uint) option);
    if (!id || (option != DCWW_WAIT && option != DCWW_NOWAIT)) return ERROR_INVALID_PARAMETER;
    for (;;) {
        grabLock(&GMutexDosCalls);
        Thread *thread;
        for (thread = GThreads; thread; thread = thread->next)
            if (*id ? thread->id == *id : thread->finished && !thread->joining) break;
        if (!thread && (*id || !GThreads)) { ungrabLock(&GMutexDosCalls); return ERROR_INVALID_THREADID; }
        if (!thread || (option == DCWW_NOWAIT && !thread->finished)) {
            ungrabLock(&GMutexDosCalls);
            if (option == DCWW_NOWAIT) return ERROR_THREAD_NOT_TERMINATED;
            lx_host_sleep_ms(1); continue;
        }
        if (thread->joining) { ungrabLock(&GMutexDosCalls); return ERROR_INVALID_THREADID; }
        LxHandleBorrow object;
        if (!lx_handle_borrow(thread->handle, LX_HANDLE_THREAD, &object)) {
            ungrabLock(&GMutexDosCalls); return ERROR_INVALID_THREADID;
        }
        thread->joining = 1;
        ungrabLock(&GMutexDosCalls);
        int joined = lx_host_thread_join(thread->thread, NULL);
        grabLock(&GMutexDosCalls);
        if (!joined) {
            for (Thread **link = &GThreads; *link; link = &(*link)->next)
                if (*link == thread) { *link = thread->next; break; }
            *id = thread->id; lx_handle_close(thread->handle, LX_HANDLE_THREAD);
        } else thread->joining = 0;
        ungrabLock(&GMutexDosCalls); lx_handle_release(&object);
        return joined ? ERROR_INVALID_THREADID : NO_ERROR;
    }
}

APIRET DosSleep(ULONG msec)
{
    TRACE_NATIVE("DosSleep(%u)", (uint) msec);

    lx_host_sleep_ms(msec);
    return NO_ERROR;
} // DosSleep

APIRET DosSubFreeMem(PVOID pbBase, PVOID pb, ULONG cb)
{
    TRACE_NATIVE("DosSubFreeMem(%p, %p, %u)", pbBase, pb, (uint) cb);
    free(pb);  // !!! FIXME: obviously wrong
    return NO_ERROR;
} // DosSubAllocMem

static APIRET DosDelete_implementation(PSZ name)
{
    if (!name) return ERROR_INVALID_PARAMETER;
    APIRET result;
    char *path = makeUnixPath(name, &result); if (!path) return result;
    result = fileError(lx_host_path_remove(path, 0)); free(path); return result;
}
APIRET DosDelete(PSZ name) { return DosDelete_implementation(name); }
APIRET DosForceDelete(PSZ name)
{
    if (!name) return ERROR_INVALID_PARAMETER;
    APIRET result;
    char *path = makeUnixPath(name, &result); if (!path) return result;
    LxHostFsError error = lx_host_path_remove(path, 0);
    if (error == LX_HOST_FS_DENIED || error == LX_HOST_FS_READ_ONLY) {
        if (!lx_host_path_readonly(path, 0)) error = lx_host_path_remove(path, 0);
    }
    result = fileError(error); free(path); return result;
}

APIRET DosSetPathInfo(PSZ pszPathName, ULONG ulInfoLevel, PVOID pInfoBuf, ULONG cbInfoBuf, ULONG flOptions)
{
    TRACE_NATIVE("DosSetPathInfo('%s', %u, %p, %u, %u)", pszPathName, (uint) ulInfoLevel, pInfoBuf, (uint) cbInfoBuf, (uint) flOptions);
    FIXME("write me");
    return NO_ERROR;
} // DosSetPathInfo

APIRET DosQueryModuleHandle(PSZ pszModname, PHMODULE phmod)
{
    TRACE_NATIVE("DosQueryModuleHandle('%s', %p)", pszModname, phmod);
    grabLock(&GMutexDosCalls);
    LxModule *lxmod;
    for (lxmod = GLoaderState.loaded_modules; lxmod; lxmod = lxmod->next) {
        if (strcasecmp(lxmod->name, pszModname) == 0)
            break;
    } // for
    ungrabLock(&GMutexDosCalls);

    if (phmod)
        *phmod = lxmod ? lxmod->handle : 0;

    return lxmod ? NO_ERROR : ERROR_INVALID_NAME;
} // DosQueryModuleHandle

static APIRET DosQueryProcAddrObject(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN* ppfn, const LxModule *lxmod)
{
    TRACE_NATIVE("DosQueryProcAddr(%u, %u, '%s', %p)", (uint) hmod, (uint) ordinal, pszName, ppfn);


    const LxExport *exports = lxmod->exports;
    const uint32 num_exports = lxmod->num_exports;

    if (ordinal > 65533) {
        return ERROR_INVALID_ORDINAL;  // according to the docs.
    } else if (ordinal != 0) {
        for (uint32 i = 0; i < num_exports; i++, exports++) {
            if (exports->ordinal == ordinal) {
                if (ppfn)
                    *ppfn = (PFN) (exports->indirect ? *(void **) exports->addr : exports->addr);
                return NO_ERROR;
            } // if
        } // for
        return ERROR_INVALID_ORDINAL;
    } else {
        // the docs explicitly say that you can't do name lookups in
        //  the DOSCALLS module, but whatever, I allow it.
        for (uint32 i = 0; i < num_exports; i++, exports++) {
            if (exports->name && (strcmp(exports->name, pszName) == 0)) {
                if (ppfn)
                    *ppfn = (PFN) (exports->indirect ? *(void **) exports->addr : exports->addr);
                return NO_ERROR;
            } // if
        } // for
        return ERROR_INVALID_NAME;
    } // else

    __builtin_unreachable();
} // DosQueryProcAddr
APIRET DosQueryProcAddr(HMODULE hmod, ULONG ordinal, PSZ pszName, PFN* ppfn)
{
    LxHandleBorrow module;
    if (!lx_handle_borrow(hmod, LX_HANDLE_MODULE, &module)) return ERROR_INVALID_HANDLE;
    const APIRET result = DosQueryProcAddrObject(hmod, ordinal, pszName, ppfn, module.resource);
    lx_handle_release(&module);
    return result;
}


APIRET DosQueryCp(ULONG cb, PULONG arCP, PULONG pcCP)
{
    TRACE_NATIVE("DosQueryCp(%u, %p, %p)", (uint) cb, arCP, pcCP);

    FIXME("This is mostly a stub");

    if (cb < sizeof (*arCP) * 2)
        return ERROR_CPLIST_TOO_SMALL;

    // 437 == United States codepage.
    arCP[0] = 437;  // current process codepage
    arCP[1] = 437;  // prepared system codepage

    if (pcCP)
        *pcCP = sizeof (*arCP) * 2;

    return NO_ERROR;
} // DosQueryCp

APIRET DosOpenL(PSZ pszFileName, PHFILE pHf, PULONG pulAction, LONGLONG cbFile, ULONG ulAttribute, ULONG fsOpenFlags, ULONG fsOpenMode, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosOpenL('%s', %p, %p, %llu, %u, %u, %u, %p)", pszFileName, pHf, pulAction, (unsigned long long) cbFile, (uint) ulAttribute, (uint) fsOpenFlags, (uint) fsOpenMode, peaop2);
    return doDosOpen(pszFileName, pHf, pulAction, cbFile, ulAttribute, fsOpenFlags, fsOpenMode, peaop2);
} // DosOpenL

static inline ULONG findNextSizeNeeded(const ULONG level, const ULONG count)
{
    const ULONG minimum = level == (ULONG) -1 ? 24 : level == FIL_STANDARD ? 32 :
                          level == FIL_QUERYEASIZE ? 36 : 0;
    return !minimum || count > UINT32_MAX / minimum ? UINT32_MAX : count * minimum;
}

static int wildcardMatch(const char *str, const char *pattern)
{
    // !!! FIXME: the way OS/2 matches files is sort of complex, and I'm not sure this is 100% right.
    int ext = 0;
    while (1) {
        const char pat = *(pattern++);
        if (pat == '\0') {
            return (*str == '\0') ? 1 : 0;
        } else if (pat == '?') {
            if (*str)  // also matches end of string, but don't advance.
                str++;
        } else if ((pat == '.') && (!ext)) {
            ext = 1;
            if (*str == '.')
                str++;
            else if (*str != '\0')
                return 0;
        } else if (pat == '*') {
            const char next = *pattern;
            while (*str && (*str != next))
                str++;
        } else {
            const char ch = *str;
            const char a = ((ch >= 'A') && (ch <= 'Z')) ? (ch - ('A' - 'a')) : ch;
            const char b = ((pat >= 'A') && (pat <= 'Z')) ? (pat - ('A' - 'a')) : pat;
            if (a == b)
                str++;
            else
                return 0;
        } // else
    } // while

    __builtin_unreachable();
} // wildcardMatch

static int findNextOne(DirFinder *dirf, PVOID *ppfindbuf, PULONG pcbBuf, int *err)
{
    const ULONG attr = dirf->attr;
    const ULONG musthave = (attr & 0xFF00) >> 8;
    const ULONG mayhave = (attr & 0x00FF);

    // these attributes mean nothing on Unix, so if they are Must Haves, return nothing.
    if (musthave & (FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED))
        return 0;

    const char *pattern = dirf->pattern, *name;
    LxHostFileInfo statbuf; LxHostFsError error = LX_HOST_FS_OK;
    while (lx_host_directory_read(dirf->dirp, &name, &statbuf, &error) == 1) {
        // !!! FIXME: OS/2 doesn't enumerate ".." for the drive root!
        //if (strcmp(name, "..") == 0) { if drive_root continue; }

        const size_t namelen = strlen(name);
        if (namelen >= CCHMAXPATHCOMP) continue;

        if (!wildcardMatch(name, pattern)) continue;

        const int isfile = statbuf.kind == LX_HOST_FILE_REGULAR;
        const int isdir = statbuf.kind == LX_HOST_FILE_DIRECTORY;
        if (!isfile && !isdir) continue;  // OS/2 didn't have fifos, devs, sockets, etc.
        assert(isfile != isdir);

        if (isdir) {
            if ((mayhave & FILE_DIRECTORY) == 0) continue;
        } else {
            if (musthave & FILE_DIRECTORY) continue;
        } // else

        const int readonly = statbuf.readonly;  // !!! FIXME: not accurate...?
        if (readonly) {
            if ((mayhave & FILE_READONLY) == 0) continue;
        } else {
            if (musthave & FILE_READONLY) continue;
        } // else


        const int legacy = dirf->level == (ULONG) -1;
        const ULONG name_offset = legacy ? 23 : dirf->level == FIL_QUERYEASIZE ? 33 : 29;
        ULONG extent = name_offset + (ULONG) namelen + 1;
        if (!legacy) extent = (extent + 3) & ~3u;
        if (extent > *pcbBuf) { *err = ERROR_BUFFER_OVERFLOW; return 0; }
        uint8 *out = *ppfindbuf;
        memset(out, 0, extent);
        uint8 *fields = out + (legacy ? 0 : 4);
        FDATE date; FTIME time;
        initFileCreationDateTime(&date, &time);
        lx_guest_store_u16(fields, date.day | date.month << 5 | date.year << 9);
        lx_guest_store_u16(fields + 2, time.twosecs | time.minutes << 5 | time.hours << 11);
        unixTimeToOs2(statbuf.accessed, &date, &time);
        lx_guest_store_u16(fields + 4, date.day | date.month << 5 | date.year << 9);
        lx_guest_store_u16(fields + 6, time.twosecs | time.minutes << 5 | time.hours << 11);
        unixTimeToOs2(statbuf.modified, &date, &time);
        lx_guest_store_u16(fields + 8, date.day | date.month << 5 | date.year << 9);
        lx_guest_store_u16(fields + 10, time.twosecs | time.minutes << 5 | time.hours << 11);
        lx_guest_store_u32(fields + 12, (uint32) statbuf.size);
        lx_guest_store_u32(fields + 16, (uint32) statbuf.allocated);
        const uint32 attributes = (isdir ? FILE_DIRECTORY : 0) | (readonly ? FILE_READONLY : 0);
        if (legacy) lx_guest_store_u16(fields + 20, attributes);
        else { lx_guest_store_u32(fields + 20, attributes); lx_guest_store_u32(out, extent); }
        out[name_offset - 1] = (uint8) namelen;
        memcpy(out + name_offset, name, namelen + 1);
        *ppfindbuf = out + extent; *pcbBuf -= extent;
        return 1;
    } // while

    if (error) *err = fileError(error);
    return 0;  // No matching entries remain.
} // findNextOne
    

static APIRET findNext(DirFinder *dirf, PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames)
{
    if (!dirf->dirp) {
        *pcFileNames = 0;
        return ERROR_NO_MORE_FILES;
    } // if

    const ULONG maxents = *pcFileNames;
    ULONG i;
    int err = NO_ERROR;
    memset(pfindbuf, 0, cbBuf);
    uint8 *last = NULL;
    for (i = 0; i < maxents; i++) {
        uint8 *current = pfindbuf;
        const uint64 position = lx_host_directory_position(dirf->dirp);
        if (!findNextOne(dirf, &pfindbuf, &cbBuf, &err)) {
            if (err == ERROR_BUFFER_OVERFLOW) lx_host_directory_seek(dirf->dirp, position);
            break;
        }
        last = current;
    }
    if (last && dirf->level != (ULONG) -1) lx_guest_store_u32(last, 0);
    if (i && err == ERROR_BUFFER_OVERFLOW) err = NO_ERROR;

    *pcFileNames = i;

    if ((i == 0) && (maxents > 0) && !err) {
        lx_host_directory_close(dirf->dirp);
        dirf->dirp = NULL;
        err = ERROR_NO_MORE_FILES;
    } // if

    return err;
} // findNext

static APIRET DosFindFirstLocked(PSZ pszFileSpec, PHDIR phdir, ULONG flAttribute, PVOID pfindbuf, ULONG cbBuf, PULONG pcFileNames, ULONG ulInfoLevel)
{
    TRACE_NATIVE("DosFindFirst('%s', %p, %u, %p, %u, %p, %u)", pszFileSpec, phdir, (uint) flAttribute, pfindbuf, (uint) cbBuf, pcFileNames, (uint) ulInfoLevel);

    if (flAttribute & 0xffffc8c8)  // reserved bits that must be zero.
        return ERROR_INVALID_PARAMETER;
    else if (cbBuf > 0xFFFF)   // !!! FIXME: Control Program API Reference says this fails if > 64k, although that was obviously a 16-bit limitation.
        return ERROR_INVALID_PARAMETER;
    else if ((ulInfoLevel != FIL_STANDARD) && (ulInfoLevel != FIL_QUERYEASIZE) && (ulInfoLevel != FIL_QUERYEASFROMLIST) && (ulInfoLevel != -1)) // -1 for 16 call
        return ERROR_INVALID_PARAMETER;
    else if (!pcFileNames)
        return ERROR_INVALID_PARAMETER;
    else if (!pfindbuf && (*pcFileNames != 0))
        return ERROR_INVALID_PARAMETER;
    else if (!phdir)
        return ERROR_INVALID_PARAMETER;
    else if (!*phdir)
        return ERROR_INVALID_HANDLE;
    else if (cbBuf < findNextSizeNeeded(ulInfoLevel, *pcFileNames))
        return ERROR_BUFFER_OVERFLOW;

    if ((ulInfoLevel != FIL_STANDARD) && (ulInfoLevel != -1)) {
        FIXME("implement extended attribute support");
        return ERROR_INVALID_PARAMETER;
    } // if

    if (flAttribute == FILE_NORMAL)
        flAttribute = FILE_READONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_ARCHIVED;  // most of the "may-have" bits.

    APIRET err = NO_ERROR;
    char *path = makeUnixPath(pszFileSpec, &err);
    if (!path)
        return err;

    char *pattern = strrchr(path, '/');
    if (pattern)
        *(pattern++) = '\0';
    else
        pattern = path;

    if (strlen(pattern) >= CCHMAXPATHCOMP) {
        free(path);
        return ERROR_FILENAME_EXCED_RANGE;
    } // if

    LxHandleBorrow directory = {0};
    uint32 handle = *phdir;
    DirFinder *dirf = NULL;
    if (*phdir == HDIR_SYSTEM) {
        dirf = &GHDir1;
    } else if (*phdir == HDIR_CREATE) {
        dirf = (DirFinder *) malloc(sizeof (DirFinder));
        if (!dirf) {
            free(path);
            return ERROR_NOT_ENOUGH_MEMORY;
        } // if
        memset(dirf, '\0', sizeof (*dirf));
    } else {
        if (!lx_handle_borrow(handle, LX_HANDLE_DIRECTORY, &directory)) {
            free(path); return ERROR_INVALID_HANDLE;
        }
        dirf = directory.resource;
    } // else

    LxHostFsError error;
    LxHostDirectory *dirp = lx_host_directory_open((path == pattern) ? "." : path, &error);
    if (!dirp) {
        if (*phdir == HDIR_CREATE) free(dirf);
        free(path); lx_handle_release(&directory);
        return fileError(error);
    }

    if (dirf->dirp)
        lx_host_directory_close(dirf->dirp);

    dirf->dirp = dirp;
    dirf->attr = flAttribute;
    dirf->level = ulInfoLevel;
    strcpy(dirf->pattern, pattern);
    free(path);

    err = findNext(dirf, pfindbuf, cbBuf, pcFileNames);

    // From the Control Program API reference:
    //  "Any nonzero return code, except ERROR_EAS_DIDNT_FIT, indicates that no handle has been allocated. This includes such nonerror
    //  indicators as ERROR_NO_MORE_FILES."
    if ((err != NO_ERROR) && (err != ERROR_EAS_DIDNT_FIT)) {
        if (dirf->dirp) lx_host_directory_close(dirf->dirp);
        dirf->dirp = NULL;
        if (*phdir == HDIR_CREATE) free(dirf);
        lx_handle_release(&directory);
        return err;
    } // if

    if (err == ERROR_NO_MORE_FILES)
        return ERROR_FILE_NOT_FOUND;
    if (*phdir == HDIR_CREATE) {
        handle = lx_handle_create(LX_HANDLE_DIRECTORY, dirf, destroyDirectoryResource);
        if (!handle) { destroyDirectoryResource(dirf); return ERROR_NOT_ENOUGH_MEMORY; }
    }
    *phdir = handle;
    lx_handle_release(&directory);
    return NO_ERROR;
} // DosFindFirst

APIRET DosFindFirst(PSZ spec, PHDIR handle, ULONG attrs, PVOID data, ULONG size, PULONG count, ULONG level)
{
    grabLock(&GMutexDosCalls);
    const APIRET result = DosFindFirstLocked(spec, handle, attrs, data, size, count, level);
    ungrabLock(&GMutexDosCalls); return result;
}
APIRET DosFindNext(HDIR handle, PVOID data, ULONG size, PULONG count)
{
    grabLock(&GMutexDosCalls);
    LxHandleBorrow object = {0};
    DirFinder *directory = handle == HDIR_SYSTEM ? &GHDir1 :
        lx_handle_borrow(handle, LX_HANDLE_DIRECTORY, &object) ? object.resource : NULL;
    APIRET result;
    if (!directory) result = ERROR_INVALID_HANDLE;
    else if (size > 0xFFFF || !count || (!data && *count)) result = ERROR_INVALID_PARAMETER;
    else if (size < findNextSizeNeeded(directory->level, *count)) result = ERROR_BUFFER_OVERFLOW;
    else result = findNext(directory, data, size, count);
    lx_handle_release(&object);
    ungrabLock(&GMutexDosCalls); return result;
}
APIRET DosFindClose(HDIR handle)
{
    grabLock(&GMutexDosCalls);
    APIRET result = NO_ERROR;
    if (handle == HDIR_SYSTEM) {
        if (GHDir1.dirp) lx_host_directory_close(GHDir1.dirp);
        memset(&GHDir1, 0, sizeof GHDir1);
    } else if (!lx_handle_close(handle, LX_HANDLE_DIRECTORY)) result = ERROR_INVALID_HANDLE;
    ungrabLock(&GMutexDosCalls); return result;
}

APIRET DosDevConfig(PVOID pdevinfo, ULONG item)
{
    TRACE_NATIVE("DosDevConfig(%p, %u)", pdevinfo, (uint) item);

    if (pdevinfo == NULL)
        return ERROR_INVALID_PARAMETER;

    BYTE *info = (BYTE *) pdevinfo;
    switch (item) {
        case DEVINFO_PRINTER: *info = 0; break; // we don't report any printers.
        case DEVINFO_RS232: *info = 0; break;   // we don't report any serial ports.
        case DEVINFO_FLOPPY: *info = 0; break;  // we don't report any floppies.
        case DEVINFO_COPROCESSOR: *info = 1; break;  // We could check for this, but c'mon.
        case DEVINFO_SUBMODEL: *info = 1; break; // OS/2 Warp 4 under Vmware returns this, but I don't know what this means.
        case DEVINFO_MODEL: *info = 252; break;  // OS/2 Warp 4 under Vmware returns this, but I don't know what this means.
        case DEVINFO_ADAPTER: *info = 1; break;  // sure, you have a real display from the last 30+ years.
        default: return ERROR_INVALID_PARAMETER;
    } // switch

    return NO_ERROR;
} // DosDevConfig

APIRET DosLoadModule(PSZ pszName, ULONG cbName, PSZ pszModname, PHMODULE phmod)
{
    TRACE_NATIVE("DosLoadModule(%p, %u, '%s', %p)", pszName, (uint) cbName, pszModname, phmod);

    FIXME("improve this");
    *pszName = 0;

    FIXME("there's no mutex on this global state at the moment!");
    LxModule *lxmod = GLoaderState.loadModule(pszModname);
    if (!lxmod)
        return ERROR_BAD_FORMAT;

    *phmod = lxmod ? lxmod->handle : 0;
    return NO_ERROR;
} // DosLoadModule

static APIRET DosFreeModuleObject(HMODULE hmod, LxModule *lxmod)
{
    TRACE_NATIVE("DosFreeModule(%u)", (uint) hmod);

    if ((!hmod) || (!GLoaderState.freeModule))
        return ERROR_INVALID_HANDLE;


    LxModule *loaded;

    /* Validate the opaque handle before passing it back to the loader. */
    grabLock(&GMutexDosCalls);
    for (loaded = GLoaderState.loaded_modules; loaded; loaded = loaded->next) {
        if (loaded == lxmod)
            break;
    } // for
    ungrabLock(&GMutexDosCalls);

    if (!loaded)
        return ERROR_INVALID_HANDLE;

    GLoaderState.freeModule(lxmod);
    return NO_ERROR;
} // DosFreeModule
APIRET DosFreeModule(HMODULE hmod)
{
    LxHandleBorrow module;
    if (!lx_handle_borrow(hmod, LX_HANDLE_MODULE, &module)) return ERROR_INVALID_HANDLE;
    const APIRET result = DosFreeModuleObject(hmod, module.resource);
    lx_handle_release(&module);
    return result;
}



APIRET DosResetBuffer(HFILE id)
{
    grabLock(&GMutexDosCalls);
    APIRET result = NO_ERROR;
    const uint32 first = id == UINT32_MAX ? 0 : id, last = id == UINT32_MAX ? MaxHFiles : id + 1;
    if (id != UINT32_MAX && (id >= MaxHFiles || HFiles[id].handle <= 1)) result = ERROR_INVALID_HANDLE;
    else for (uint32 i = first; i < last; i++) {
        LxHandleBorrow file;
        if (HFiles[i].handle > 1 && lx_handle_borrow(HFiles[i].handle, LX_HANDLE_FILE, &file)) {
            LxHostFileInfo info;
            /* Pipes/devices have no filesystem buffers to commit. */
            LxHostFsError error = lx_host_file_info(file.resource, &info);
            if (!error && info.kind == LX_HOST_FILE_REGULAR) error = lx_host_file_flush(file.resource);
            lx_handle_release(&file); if (!result) result = fileError(error);
        }
    }
    ungrabLock(&GMutexDosCalls); return result;
}

APIRET DosQueryAppType(PSZ pszName, PULONG pFlags)
{
    TRACE_NATIVE("DosQueryAppType('%s', %p)", pszName, pFlags);

    if (!pFlags)
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *path = makeUnixPath(pszName, &err);
    if (!path)
        return err;

    LxHostFsError error;
    LxHostFile *file = lx_host_file_open(path, LX_HOST_FILE_READ, 0, &error);
    free(path);
    if (!file) return fileError(error);
    uint8 mz[64], lx[24]; size_t actual;
    APIRET result = ERROR_INVALID_EXE_SIGNATURE;
    if (lx_host_file_read(file, mz, sizeof mz, &actual) || actual != sizeof mz || mz[0] != 'M' || mz[1] != 'Z') goto done;
    if (lx_host_file_seek(file, lx_guest_load_u32(mz + 60), 0, NULL) ||
        lx_host_file_read(file, lx, sizeof lx, &actual) || actual != sizeof lx || lx[0] != 'L' || lx[1] != 'X') goto done;
    const uint32 module_flags = lx_guest_load_u32(lx + 16);
    if (module_flags & 0x2000) { result = ERROR_EXE_MARKED_INVALID; goto done; }
    const uint32 type = module_flags & 0x38000;
    ULONG flags = FAPPTYP_32BIT;
    switch (module_flags & 0x300) {
        case 0x100: flags |= FAPPTYP_NOTWINDOWCOMPAT; break;
        case 0x200: flags |= FAPPTYP_WINDOWCOMPAT; break;
        case 0x300: flags |= FAPPTYP_WINDOWAPI; break;
    }
    switch (type) {
        case 0x8000: flags |= FAPPTYP_DLL; break;
        case 0x20000: flags |= FAPPTYP_PHYSDRV; break;
        case 0x28000: flags |= FAPPTYP_VIRTDRV; break;
        case 0x18000: flags |= FAPPTYP_PROTDLL; break;
    }
    *pFlags = flags; result = NO_ERROR;
done:
    lx_host_file_close(file); return result;
}

APIRET DosAllocThreadLocalMemory(ULONG cb, PULONG *p)
{
    TRACE_NATIVE("DosAllocThreadLocalMemory(%u, %p)", (uint) cb, p);

    if (!cb || cb > 8)  // OS/2 allocates one to eight contiguous words.
        return ERROR_INVALID_PARAMETER;
    else if (!p)
        return ERROR_INVALID_PARAMETER;

    APIRET retval = NO_ERROR;
    uint32 mask = 0xFF >> (8 - cb);

    grabLock(&GMutexDosCalls);

    // this is probably expensive to do with the mutex held, but honestly,
    //  is there a lot of thread contention at the point where your app is
    //  allocating TLS?
    if (GLoaderState.tlspage == NULL) {
        if ((GLoaderState.tlspage = (uint32 *) (uintptr_t) lx_execution_tls_window(GLoaderState.execution)) == NULL) {
            ungrabLock(&GMutexDosCalls);
            return ERROR_NOT_ENOUGH_MEMORY;
        } // if
    } // if

    int i;
    for (i = 0; i <= 32 - (int) cb; i++, mask <<= 1) {
        if (((GLoaderState.tlsmask ^ mask) & mask) == mask)
            break;
    } // for

    if (i > 32 - (int) cb)
        retval = ERROR_NOT_ENOUGH_MEMORY; // there are only 32 slots, couldn't find anything contiguous.
    else {
        assert(GLoaderState.tlsallocs[i] == 0);
        GLoaderState.tlsallocs[i] = (uint8) cb;
        GLoaderState.tlsmask |= mask;
        *p = GLoaderState.tlspage + i;
    } // else
        
    ungrabLock(&GMutexDosCalls);

    return retval;
} // DosAllocThreadLocalMemory

APIRET DosFreeThreadLocalMemory(ULONG *p)
{
    TRACE_NATIVE("DosFreeThreadLocalMemory(%p)", p);

    APIRET retval = ERROR_INVALID_PARAMETER;

    grabLock(&GMutexDosCalls);

    if (GLoaderState.tlspage) {
        const uintptr_t distance = (uintptr_t) p - (uintptr_t) GLoaderState.tlspage;
        const uint32 slot = distance < 128 && !(distance & 3) ? (uint32) distance / 4 : 32;
        if (slot < 32) {
            const uint8 slots = GLoaderState.tlsallocs[slot];
            if (slots > 0) {
                const uint32 mask = ((uint32) (0xFF >> (8 - slots))) << slot;
                assert((GLoaderState.tlsmask & mask) == mask);
                GLoaderState.tlsmask &= ~mask;
                GLoaderState.tlsallocs[slot] = 0;
                retval = NO_ERROR;
            } // if
        } // if
    } // if

    ungrabLock(&GMutexDosCalls);

    return retval;
} // DosFreeThreadLocalMemory

APIRET DosQueryFHState(HFILE hFile, PULONG pMode)
{
    TRACE_NATIVE("DosQueryFHState(%u, %p)", (uint) hFile, pMode);

    APIRET retval = NO_ERROR;
    ULONG tmp = 0;
    if (!pMode)
        pMode = &tmp;

    grabLock(&GMutexDosCalls);
    if ((hFile < MaxHFiles) && (HFiles[hFile].handle > 1))
        *pMode = HFiles[hFile].flags;
    else
        retval = ERROR_INVALID_HANDLE;
    ungrabLock(&GMutexDosCalls);

    return retval;
} // DosQueryFHState

APIRET DosQueryHeaderInfo(HMODULE hmod, ULONG ulIndex, PVOID pvBuffer, ULONG cbBuffer, ULONG ulSubFunction)
{
    TRACE_NATIVE("DosQueryHeaderInfo(%u, %u, %p, %u, %u)", (uint) hmod, (uint) ulIndex, pvBuffer, (uint) cbBuffer, (uint) ulSubFunction);

    switch (ulSubFunction) {
        //case QHINF_EXEINFO:
        //case QHINF_READRSRCTBL:
        //case QHINF_READFILE:

        case QHINF_LIBPATHLENGTH:
            if (cbBuffer < sizeof (ULONG))
                return ERROR_BUFFER_OVERFLOW;
            *((ULONG *) pvBuffer) = GLoaderState.libpathlen;
            return NO_ERROR;

        case QHINF_LIBPATH:
            if (cbBuffer < GLoaderState.libpathlen)
                return ERROR_BUFFER_OVERFLOW;
            strcpy((char *) pvBuffer, GLoaderState.libpath);
            return NO_ERROR;

        //case QHINF_FIXENTRY:
        //case QHINF_STE:
        //case QHINF_MAPSEL:

        default: FIXME("I don't know what this query wants"); break;
    } // switch

    return ERROR_INVALID_PARAMETER;
} // DosQueryHeaderInfo

APIRET DosQueryExtLIBPATH(PSZ pszExtLIBPATH, ULONG flags)
{
    TRACE_NATIVE("DosQueryExtLIBPATH('%s', %u)", pszExtLIBPATH, (uint) flags);

    // !!! FIXME: this is mostly a stub for now.

    if ((flags != BEGIN_LIBPATH) && (flags != END_LIBPATH))
        return ERROR_INVALID_PARAMETER;

    if (pszExtLIBPATH)
        *pszExtLIBPATH = '\0';

    return NO_ERROR;
} // DosQueryExtLIBPATH

APIRET DosSetMaxFH(ULONG cFH)
{
    TRACE_NATIVE("DosSetMaxFH(%u)", (uint) cFH);
    grabLock(&GMutexDosCalls);

    if (cFH < MaxHFiles || cFH > 0x10000u) {
        ungrabLock(&GMutexDosCalls);
        return ERROR_INVALID_PARAMETER;  // strictly speaking, we could shrink, but I'm not doing it.
    } // if

    if (cFH == MaxHFiles) {
        ungrabLock(&GMutexDosCalls);
        return NO_ERROR;
    } // if

    HFileInfo *info = (HFileInfo *) realloc(HFiles, sizeof (HFileInfo) * (cFH));
    if (info != NULL) {
        HFiles = info;
        info += MaxHFiles;
        for (ULONG i = MaxHFiles; i < cFH; i++, info++) {
            info->handle = 0;
            info->type = 0;
            info->attr = 0;
            info->flags = 0;
        } // for
        MaxHFiles = cFH;
    } // if

    ungrabLock(&GMutexDosCalls);

    return NO_ERROR;
} // DosSetMaxFH

APIRET DosQuerySysState(ULONG func, ULONG arg1, ULONG pid, ULONG _res_, PVOID buf, ULONG bufsz)
{
    TRACE_NATIVE("DosQuerySysState(%u, %u, %u, %u, %p, %u)", (uint) func, (uint) arg1, (uint) pid, (uint) _res_, buf, (uint) bufsz);
    FIXME("implement me");
    return ERROR_INVALID_PARAMETER;
} // DosQuerySysState

APIRET DosR3ExitAddr(void)
{
    TRACE_NATIVE("DosR3ExitAddr()");
    FIXME("I have no idea what this is");  // but...JAVA USES IT.
    return ERROR_INVALID_PARAMETER;
} // DosR3ExitAddr

APIRET DosQueryThreadContext(TID tid, ULONG level, PCONTEXTRECORD pcxt)
{
    TRACE_NATIVE("DosQueryThreadContext(%u, %u, %p)", (uint) tid, (uint) level, pcxt);
    FIXME("Need to be able to suspend threads first");
    return ERROR_INVALID_PARAMETER;
} // DosQueryThreadContext

static APIRET DosQueryCurrentDir_implementation(ULONG disknum, PBYTE pBuf, PULONG pcbBuf)
{
    if (disknum > (sizeof (GLoaderState.disks) / sizeof (GLoaderState.disks[0]))) {
        return ERROR_INVALID_DRIVE;
    }

    const char *cdir = GLoaderState.current_dir[disknum ? (disknum-1) : GLoaderState.current_disk];
    if (cdir == NULL) {
        FIXME("should this return ERROR_INVALID_DRIVE?");
        return ERROR_NOT_DOS_DISK;
    }

    const ULONG pathlen = (ULONG) strlen(cdir);
    if (*pcbBuf <= pathlen) {
        *pcbBuf = pathlen;
        return ERROR_BUFFER_OVERFLOW;
    }

    strcpy((char *) pBuf, cdir);
    *pcbBuf = pathlen;
    return NO_ERROR;
} // DosQueryCurrentDir_implementation

APIRET DosQueryCurrentDir(ULONG disknum, PBYTE pBuf, PULONG pcbBuf)
{
    TRACE_NATIVE("DosQueryCurrentDir(%u, %p, %p)", (uint) disknum, pBuf, pcbBuf);
    return DosQueryCurrentDir_implementation(disknum, pBuf, pcbBuf);
} // DosQueryCurrentDir

static APIRET DosQueryCurrentDisk_implementation(PULONG pdisknum, PULONG pdiskmap)
{
    *pdisknum = GLoaderState.current_disk;
    *pdiskmap = GLoaderState.diskmap;
    return NO_ERROR;
} // DosQueryCurrentDisk_implementation

APIRET DosQueryCurrentDisk(PULONG pdisknum, PULONG pdiskmap)
{
    TRACE_NATIVE("DosQueryCurrentDisk(%p, %p)", pdisknum, pdiskmap);
    return DosQueryCurrentDisk_implementation(pdisknum, pdiskmap);
} // DosQueryCurrentDisk

static APIRET DosSetCurrentDir_implementation(PSZ pszName)
{
    // OS/2 reports failure for changing to a path that ends in '\\' even if
    //  the rest of the path is a valid dir. Linux does not, so we explicitly
    //  check it here. However, "\\" by itself is valid (as the absolute path
    //  to the disk's root dir).

    if (*pszName == '\0') {
        return NO_ERROR;  // I guess this is a no-op...?
    } else if ((pszName[0] == '\\') && (pszName[1] == '\0')) {
        // this is okay.
    } else if (pszName[strlen(pszName) - 1] == '\\') {
        return ERROR_PATH_NOT_FOUND;
    }

    FIXME("This function needs a lock");  // in case the cwd changes from another thread.

    uint32 err = 0;
    char *path = makeUnixPath(pszName, &err);
    if (!path) return (APIRET) err;

    // generally we don't use the Unix process's cwd, but this might be
    //  helpful for subprocesses or native builds, but mostly, it checks
    //  all the stuff we care about (missing dir, not a dir, etc).
    LxHostFsError changed = lx_host_path_chdir(path);
    APIRET retval = changed == LX_HOST_FS_NOT_DIRECTORY || changed == LX_HOST_FS_MISSING ? ERROR_PATH_NOT_FOUND : fileError(changed);

    free(path);

    if (retval == NO_ERROR) {
        char drive = pszName[0];
        if ((drive >= 'a') && (drive <= 'z')) {
            drive += 'A' - 'a';
        }

        if (((drive >= 'A') && (drive <= 'Z')) && (pszName[1] == ':')) {  // it's a drive letter.
            pszName += 2;  // skip drive letter and colon.
        } else {
            drive = (GLoaderState.current_disk-1) + 'A';
        }

        const int driveidx = drive - 'A';
        char *oldpath = (*pszName == '\\') ? "" : GLoaderState.current_dir[driveidx];
        const size_t buflen = strlen(pszName) + strlen(oldpath) + 2;
        char *newpath = (char *)  malloc(buflen);
        if (!newpath) {
            return ERROR_NOT_ENOUGH_MEMORY;
        }

        // set up the string we'll use for the cwd. Normalize out "." and ".." chars, append to existing path
        //  if we're relative, etc.
        snprintf(newpath, buflen, "%s%s%s", oldpath, *oldpath ? "\\" : "", pszName);

        char *ptr = newpath;

        // strip out double '\\' chars.
        while ( (ptr = strstr(ptr, "\\\\")) != NULL ) {
            memmove(ptr, ptr + 1, strlen(ptr));
        }

        // trim '\\' char off the start of the string if it exists.
        if (*newpath == '\\') {
            memmove(newpath, newpath + 1, strlen(newpath));
        }

        // strip out "." and ".." dirs.
        char *element = newpath;
        ptr = newpath;
        while ( (ptr = strchr(ptr, '\\')) != NULL ) {
            *ptr = '\0';
            if (strcmp(element, ".") == 0) {  // dump "." subdir.
                memmove(element, ptr + 1, strlen(ptr + 1) + 1);
                ptr = element;
            } else if (strcmp(element, "..") == 0) {  // dump ".." subdir and parent.
                if (element == newpath) {  // no parent? Just dump "..".
                    memmove(newpath, ptr + 1, strlen(ptr + 1) + 1);
                    ptr = newpath;
                } else {
                    assert(((int)(element - newpath)) >= 2);  // shouldn't be "\\" at start of string.
                    char *prev = element - 2;
                    while ((prev != newpath) && (*prev != '\\')) {
                        prev--;
                    }
                    if (prev != newpath) {
                        prev++;
                    }
                    memmove(prev, ptr + 1, strlen(ptr + 1) + 1);
                    ptr = prev;
                }
            } else {
                *ptr = '\\';  // just a normal dir, restore it.
                ptr++;
            }
            element = ptr;
        }

        // check last element, when strchr fails because there wasn't any more '\\' chars...
        if (strcmp(element, ".") == 0) {
            if (element != newpath) {
                element--;  // so we eat the '\\' char
            }
            *element = '\0';
        } else if (strcmp(element, "..") == 0) {
            if (element == newpath) {  // no parent? Just dump the whole string.
                *newpath = '\0';
            } else {
                assert(((int)(element - newpath)) >= 2);  // shouldn't be "\\" at start of string.
                char *prev = element - 2;
                while ((prev != newpath) && (*prev != '\\')) {
                    prev--;
                }
                *prev = '\0';  // dump parent (if any) and ".."
            }
        }

        ptr = (char *) realloc(newpath, strlen(newpath) + 1);  // shrink if possible.
        if (ptr) {
            newpath = ptr;
        }

        //printf("CHDIR: Normalized '%s' + '%s' to '%s'\n", oldpath, pszName, newpath);
        free(GLoaderState.current_dir[driveidx]);
        GLoaderState.current_dir[driveidx] = newpath;
    }

    return retval;
} // DosSetCurrentDir_implementation

APIRET DosSetCurrentDir(PSZ pszName)
{
    TRACE_NATIVE("DosSetCurrentDir('%s')", pszName);
    return DosSetCurrentDir_implementation(pszName);
} // DosSetCurrentDir


static APIRET DosCreateDir_implementation(PSZ pszDirName, PEAOP2 peaop2)
{
    if (peaop2 != NULL) {
        FIXME("EAs not yet implemented");
        return ERROR_EA_LIST_INCONSISTENT;
    }

    uint32 err = 0;
    char *path = makeUnixPath(pszDirName, &err);
    if (!path) return (APIRET) err;

    const APIRET result = fileError(lx_host_path_mkdir(path));
    free(path); return result;
} // DosCreateDir_implementation


APIRET DosCreateDir(PSZ pszDirName, PEAOP2 peaop2)
{
    TRACE_NATIVE("DosCreateDir('%s', %p)", pszDirName, peaop2);
    return DosCreateDir_implementation(pszDirName, peaop2);
} // DosCreateDir



#if !LX_LEGACY
ULONG DosSelToFlat(VOID) { return 0; }
#else
ULONG _DosSelToFlat(void *ptr)
{
    TRACE_NATIVE("DosSelToFlat(%p)", ptr);
    return (ULONG) GLoaderState.convert1616to32((uint32) ptr);
} // _DosSelToFlat


#endif

static int semaphoreWord(PHSEM16 sem, int exchange, uint32 value, uint32 *previous)
{
    if (!sem) return 0;
    if (GLoaderState.execution) {
        uint32 address;
        return lx_execution_address(GLoaderState.execution, sem, 4, 3, &address) &&
               lx_execution_atomic_u32(GLoaderState.execution, address, exchange, value, previous);
    }
    grabLock(&GMutexDosCalls);
    *previous = lx_guest_load_u32(sem);
    if (exchange) lx_guest_store_u32(sem, value);
    ungrabLock(&GMutexDosCalls);
    return 1;
}
static APIRET16 waitSemaphoreWord(PHSEM16 sem, LONG milliseconds, int request)
{
    const uint64 deadline = milliseconds < 0 ? UINT64_MAX :
        lx_host_monotonic_us() + (uint64) milliseconds * 1000;
    for (;;) {
        uint32 previous;
        if (!semaphoreWord(sem, request, 1, &previous)) return ERROR_INVALID_PARAMETER;
        if (!previous) return NO_ERROR;
        if (milliseconds == 0 || lx_host_monotonic_us() >= deadline) return ERROR_TIMEOUT;
        lx_host_sleep_ms(1);
    }
}
APIRET16 DosSemRequest(PHSEM16 sem, LONG ms)
{
    TRACE_NATIVE("DosSemRequest(%p, %d)", sem, (int) ms);
    return waitSemaphoreWord(sem, ms, 1);
}
APIRET16 DosSemClear(PHSEM16 sem)
{
    TRACE_NATIVE("DosSemClear(%p)", sem);
    uint32 previous;
    return semaphoreWord(sem, 1, 0, &previous) ? NO_ERROR : ERROR_INVALID_PARAMETER;
}
APIRET16 DosSemWait(PHSEM16 sem, LONG ms)
{
    TRACE_NATIVE("DosSemWait(%p, %d)", sem, (int) ms);
    return waitSemaphoreWord(sem, ms, 0);
}
APIRET16 DosSemSet(PHSEM16 sem)
{
    TRACE_NATIVE("DosSemSet(%p)", sem);
    uint32 previous;
    return semaphoreWord(sem, 1, 1, &previous) ? NO_ERROR : ERROR_INVALID_PARAMETER;
}

static int mutexCanClose(void *value) { return !lx_host_mutex_busy(value); }
APIRET DosCloseMutexSem(HMTX handle)
{
    TRACE_NATIVE("DosCloseMutexSem(%u)", (uint) handle);
    LxHandleBorrow object;
    if (!lx_handle_borrow(handle, LX_HANDLE_MUTEX, &object)) return ERROR_INVALID_HANDLE;
    grabLock(&GMutexDosCalls);
    int closed = lx_handle_close_guarded(handle, LX_HANDLE_MUTEX, 1, mutexCanClose);
    if (closed == 1) retireSemaphoreName(object.resource, LX_HANDLE_MUTEX);
    ungrabLock(&GMutexDosCalls);
    lx_handle_release(&object);
    return closed == 1 ? NO_ERROR : closed == -1 ? ERROR_SEM_BUSY : ERROR_INVALID_HANDLE;
}

APIRET DosSetProcessCp(ULONG cp)
{
    TRACE_NATIVE("DosSetProcessCp(%u)", (unsigned int) cp);
    if (cp != 437) {  // United States
        FIXME("non-US codepages unsupported at the moment");
        return ERROR_INVALID_CODE_PAGE;
    }
    return NO_ERROR;
} // DosSetProcessCp

APIRET DosQueryFSAttach(PSZ pszDeviceName, ULONG ulOrdinal, ULONG ulFSAInfoLevel, PFSQBUFFER2 pfsqb, PULONG pcbBuffLength)
{
    TRACE_NATIVE("DosQueryFSAttach('%s', %u, %u, %p, %p)", pszDeviceName, (unsigned int) ulOrdinal, (unsigned int) ulFSAInfoLevel, pfsqb, pcbBuffLength);
    return ERROR_INVALID_FUNCTION;
} // DosQueryFSAttach

APIRET DosSetFileSize(HFILE id, ULONG size)
{
    LxHandleBorrow file;
    if (!borrowFile(id, &file)) return ERROR_INVALID_HANDLE;
    const LxHostFsError error = lx_host_file_resize(file.resource, size);
    lx_handle_release(&file); return fileError(error);
}
static APIRET DosDupHandle_implementation(HFILE source, PHFILE output)
{
    if (!output) return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDosCalls);
    APIRET result = NO_ERROR; HFILE target = *output;
    LxHandleBorrow original = {0};
    if (source >= MaxHFiles || HFiles[source].handle <= 1 ||
        !lx_handle_borrow(HFiles[source].handle, LX_HANDLE_FILE, &original)) result = ERROR_INVALID_HANDLE;
    else {
        if (target == (HFILE) -1) {
            for (target = 0; target < MaxHFiles && HFiles[target].handle; target++) {}
            if (target == MaxHFiles) result = ERROR_TOO_MANY_OPEN_FILES;
        } else if (target >= MaxHFiles || HFiles[target].handle) result = ERROR_INVALID_TARGET_HANDLE;
        if (!result) {
            LxHostFsError error;
            LxHostFile *copy = lx_host_file_duplicate(original.resource, &error);
            if (!copy) result = fileError(error);
            else {
                HFileInfo info;
                if (!initFileInfo(copy, &info, HFiles[source].flags)) result = ERROR_NOT_ENOUGH_MEMORY;
                else { HFiles[target] = info; *output = target; }
            }
        }
    }
    lx_handle_release(&original); ungrabLock(&GMutexDosCalls); return result;
}

APIRET DosDupHandle(HFILE hFile, PHFILE pHfile)
{
    TRACE_NATIVE("DosDupHandle(%u, %p)", hFile, pHfile);
    return DosDupHandle_implementation(hFile, pHfile);
} // DosDupHandle


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

static DosBeepGeneratorInfo dos_beep_info;

static int DosBeepGenerator(void *userdata, float *stream, int samples, int freq)
{
    const float beep_volume = GLoaderState.beep_volume;
    DosBeepGeneratorInfo *info = &dos_beep_info;
    assert(userdata == &dos_beep_info);

    while (samples > 0) {
        if (!info->samples_remaining) {  // current beep command is finished?
            if (!info->next_freq) {
                return 0;  // out of things to play, remove ourselves from the mixer.
            }

            // set up next beep command.
            info->square_length = ((ULONG) freq) / info->next_freq;
            info->square_remainder = ((ULONG) freq) % info->next_freq;
            info->samples_remaining = ((ULONG) ((info->next_duration / 1000.0) * ((ULONG) freq))) * 1;
            info->accumulator = 0;
            info->next_freq = 0;
            info->next_duration = 0;
        } else if (!info->square_remaining) {  // this part of the square wave is finished, flip it and start the next part.
            info->positive = !info->positive;
            info->square_remaining = info->square_length;
            info->accumulator += info->square_remainder;
            if (info->accumulator >= info->square_length) {
                info->accumulator -= info->square_length;
                info->square_remaining++;
            }
        } else {  // still generating the current piece of the square wave, mix it into the stream.
            // VMware emulates the PC Speaker on a sound card _really_ quietly.
            const float val = info->positive ? beep_volume : -beep_volume;
            int total = info->square_remaining;
            if (info->samples_remaining < total) total = info->samples_remaining;
            if (samples < total) total = samples;

            FIXME("good candidate for SSE");
            for (int i = 0; i < total; i++) {
                stream[i] += val;
            }

            samples -= total;
            info->samples_remaining -= total;
            info->square_remaining -= total;
            stream += total;
        }
    }

    return 1;  // remain in the mixer to keep generating audio.
} // DosBeepGenerator

static APIRET DosBeep_implementation(ULONG freq, ULONG duration)
{
    if ((freq < 0x25) || (freq > 0x7FFF)) {
        return ERROR_INVALID_FREQUENCY;
    } else if (duration == 0) {
        return NO_ERROR;
    }

    // Either OS/2 or Vmware seems to be doubling the frequency requested, I
    //  think, as this sounds closer to what DosBeep() generates there.
    //FIXME("sanity check this");
    //freq *= 2;

    // assuming OS/2 ran the PC Speaker off the BIOS timer, firing 18.2 times
    //  a second (about 55 milliseconds), we'll round up the requested
    //  duration to match.
    FIXME("sanity check this");
    const ULONG duration_diff = duration % 55;
    const ULONG mix_duration = duration + (duration_diff ? (55 - duration_diff) : 0);

    // we chain beep commands together and return a little before they'll
    //  end, so the caller can queue another without a gap in playback if
    //  they're making multiple calls to DosBeep to make a cute little tune.

    // OS/2's implementation appears to hold a mutex while driving the PC
    //  speaker; if you DosBeep() from two threads at once, one will block
    //  until the other finishes playing.
    grabLock(&GMutexDosBeep);

    FIXME("should be atomic");
    while (*((volatile ULONG *) &dos_beep_info.next_freq) != 0) {
        lx_host_sleep_ms(1);
    }

    dos_beep_info.next_duration = mix_duration;
    dos_beep_info.next_freq = freq;

    // wait about the time this was requested to play, but since there is
    //  variance on when the audio thread will run again, and we padded out
    //  the play time to 55 milliseconds, chances are good that the next
    //  DosBeep call will queue more sound to play seamlessly.
    // If less than 50 milliseconds, don't sleep at all; if they call DosBeep
    //  again, it'll block until it can queue the command anyhow.
    if (duration > 55) {
        lx_host_sleep_ms(duration - 55);
    }

    const int rc = GLoaderState.registerAudioGenerator(DosBeepGenerator, &dos_beep_info, 1);
    if (!rc) {
        memset(&dos_beep_info, '\0', sizeof (dos_beep_info));  // oh well.
    }

    ungrabLock(&GMutexDosBeep);

    return NO_ERROR;
} // DosBeep_implementation

APIRET DosBeep(ULONG freq, ULONG duration)
{
    TRACE_NATIVE("DosBeep(%u, %u)", freq, duration);
    return DosBeep_implementation(freq, duration);
} // DosBeep

static LxHostOnce GCritSecOnce = LX_HOST_ONCE_INIT;
static LxHostMutex *GCritSec;

static void initCritSec(void)
{
    GCritSec = lx_host_mutex_create(1, 0);
    if (!GCritSec) abort();
}

APIRET16 Dos16EnterCritSec(VOID)
{
    TRACE_NATIVE("Dos16EnterCritSec()");
    lx_host_once(&GCritSecOnce, initCritSec);
    lx_host_mutex_lock(GCritSec, UINT32_MAX);
    return NO_ERROR;
} // Dos16EnterCritSec

APIRET16 Dos16ExitCritSec(VOID)
{
    TRACE_NATIVE("Dos16ExitCritSec()");
    lx_host_once(&GCritSecOnce, initCritSec);
    lx_host_mutex_unlock(GCritSec);
    return NO_ERROR;
} // Dos16ExitCritSec

APIRET16 Dos16SetPrty(USHORT scope, USHORT prtyclass, SHORT delta, USHORT pid)
{
    TRACE_NATIVE("Dos16SetPrty(%u, %u, %d, %u)", (uint) scope,
                 (uint) prtyclass, (int) delta, (uint) pid);
    return NO_ERROR;
} // Dos16SetPrty

APIRET16 Dos16GetDateTime(PDATETIME pdt)
{
    TRACE_NATIVE("Dos16GetDateTime(%p)", pdt);
    return (APIRET16) DosGetDateTime(pdt);
} // Dos16GetDateTime

APIRET16 Dos16Error(USHORT error)
{
    TRACE_NATIVE("Dos16Error(%u)", (uint) error);
    return (APIRET16) DosError((ULONG) error);
} // Dos16Error

APIRET16 Dos16GetSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16GetSeg(%u)", (uint) sel);
    return NO_ERROR;
} // Dos16GetSeg

APIRET16 Dos16LockSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16LockSeg(%u)", (uint) sel);
    return NO_ERROR;
} // Dos16LockSeg

APIRET16 Dos16UnlockSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16UnlockSeg(%u)", (uint) sel);
    return NO_ERROR;
} // Dos16UnlockSeg

APIRET16 Dos16HugeShift(PUSHORT pshift)
{
    TRACE_NATIVE("Dos16HugeShift(%p)", pshift);
    if (pshift)
        *pshift = 0;
    return NO_ERROR;
} // Dos16HugeShift

APIRET16 Dos16HugeIncr(USHORT sel, PULONG psize)
{
    TRACE_NATIVE("Dos16HugeIncr(%u, %p)", (uint) sel, psize);
    return NO_ERROR;
} // Dos16HugeIncr

APIRET16 Dos16GetDBCSEv(USHORT cb, PVOID pcc, PVOID buf)
{
    TRACE_NATIVE("Dos16GetDBCSEv(%u, %p, %p)", (uint) cb, pcc, buf);
    (void) pcc;
    if (buf && cb)
        memset(buf, 0, cb);
    return NO_ERROR;
} // Dos16GetDBCSEv

APIRET16 Dos16DevIOCtl(PVOID pdata, PVOID pparms, USHORT fun, USHORT category, USHORT hdev)
{
    TRACE_NATIVE("Dos16DevIOCtl(%p, %p, %u, %u, %u)", pdata, pparms,
                 (uint) fun, (uint) category, (uint) hdev);
    return ERROR_INVALID_FUNCTION;
} // Dos16DevIOCtl

static void storeDateTime16(uint8 *data, FDATE date, FTIME time)
{
    lx_guest_store_u16(data, date.day | date.month << 5 | date.year << 9);
    lx_guest_store_u16(data + 2, time.twosecs | time.minutes << 5 | time.hours << 11);
}
APIRET16 Dos16QFileInfo(USHORT hf, USHORT level, PVOID buffer, USHORT capacity)
{
    const uint32 size = level == FIL_STANDARD ? 22 : level == FIL_QUERYEASIZE ? 26 : 0;
    if (!size) return ERROR_INVALID_LEVEL;
    if (capacity < size) return ERROR_BUFFER_OVERFLOW;
    uint32 address;
    if (!lx_execution_address(GLoaderState.execution, buffer, size, LX_GUEST_WRITE, &address))
        return ERROR_INVALID_PARAMETER;
    FILESTATUS4 value = {0};
    const APIRET result = DosQueryFileInfo(hf, level, &value, sizeof value);
    if (!result) {
        uint8 *p = buffer;
        storeDateTime16(p, value.fdateCreation, value.ftimeCreation);
        storeDateTime16(p + 4, value.fdateLastAccess, value.ftimeLastAccess);
        storeDateTime16(p + 8, value.fdateLastWrite, value.ftimeLastWrite);
        lx_guest_store_u32(p + 12, value.cbFile); lx_guest_store_u32(p + 16, value.cbFileAlloc);
        lx_guest_store_u16(p + 20, value.attrFile);
        if (level == FIL_QUERYEASIZE) lx_guest_store_u32(p + 22, value.cbList);
    }
    return result;
}
APIRET16 Dos16SetFileInfo(USHORT hf, USHORT level, PVOID buffer, USHORT capacity)
{
    if (level != FIL_STANDARD) return ERROR_INVALID_LEVEL;
    if (capacity < 22) return ERROR_BUFFER_OVERFLOW;
    uint32 address;
    if (!lx_execution_address(GLoaderState.execution, buffer, 22, LX_GUEST_READ, &address))
        return ERROR_INVALID_PARAMETER;
    FILESTATUS3 value = {0};
    /* The current file-info setter applies the permission attribute. */
    value.attrFile = lx_guest_load_u16((uint8 *) buffer + 20);
    return DosSetFileInfo(hf, level, &value, sizeof value);
}

APIRET16 Dos16SelectDisk(USHORT drive)
{
    TRACE_NATIVE("Dos16SelectDisk(%u)", (uint) drive);
    return (APIRET16) DosSetDefaultDisk((ULONG) drive);
} // Dos16SelectDisk

APIRET16 Dos16SetMaxFH(USHORT cfh)
{
    TRACE_NATIVE("Dos16SetMaxFH(%u)", (uint) cfh);
    return (APIRET16) DosSetMaxFH((ULONG) cfh);
} // Dos16SetMaxFH

APIRET16 Dos16DevConfig(PVOID pdevinfo, USHORT item, USHORT reserved)
{
    TRACE_NATIVE("Dos16DevConfig(%p, %u, %u)", pdevinfo, (uint) item, (uint) reserved);
    return (APIRET16) DosDevConfig(pdevinfo, (ULONG) item);
} // Dos16DevConfig

APIRET16 Dos16NewSize(USHORT handle, ULONG size)
{
    return DosSetFileSize(handle, size);
} // Dos16NewSize

APIRET16 Dos16CreateCSAlias(USHORT selDS, PUSHORT pselCS)
{
    TRACE_NATIVE("Dos16CreateCSAlias(%u, %p)", (uint) selDS, pselCS);
    uint16 alias = selDS;
    if (GLoaderState.createCodeAlias)
        GLoaderState.createCodeAlias(selDS, &alias);
    if (pselCS)
        *pselCS = alias;
    return NO_ERROR;
} // Dos16CreateCSAlias

typedef struct NeThreadStart
{
    uint32 entry, stack;
    void *invocation;
    uint16 tid, tib_selector;
} NeThreadStart;

static void neThreadCleanup(void *argument)
{
    NeThreadStart *start = argument;
    GLoaderState.deinitOs2Tib(start->tib_selector);
    lx_execution_destroy_thread16(GLoaderState.execution, start->invocation); free(start);
}
static void *neThreadEntry(void *argument)
{
    NeThreadStart *start = argument;
    uint8 tibspace[LXTIBSIZE] = {0};
    GLoaderState.initOs2Tib(tibspace, GLoaderState.convert1616to32(start->stack), 65536, start->tid);
    start->tib_selector = GLoaderState.setOs2Tib(tibspace);
    lx_execution_thread16(GLoaderState.execution, start->invocation); return NULL;
}

APIRET16 Dos16CreateThread(PVOID pfnFun, PVOID pTid, PVOID pbStack)
{
    TRACE_NATIVE("Dos16CreateThread(%p, %p, %p)", pfnFun, pTid, pbStack);
    if (!pfnFun || !pTid || !pbStack)
        return ERROR_INVALID_PARAMETER;
    NeThreadStart *start = calloc(1, sizeof (*start));
    if (!start)
        return ERROR_NOT_ENOUGH_MEMORY;
    grabLock(&GMutexDosCalls);
    const uint32 tid = GNextThreadId++; ungrabLock(&GMutexDosCalls);
    if (tid > 0xffff) {
        free(start);
        return ERROR_MAX_THRDS_REACHED;
    }
    start->tid = (uint16) tid;
    start->entry = GLoaderState.convert32to1616(pfnFun);
    start->stack = GLoaderState.convert32to1616(pbStack);
    if (start->entry && start->stack)
        start->invocation = lx_execution_prepare_thread16(GLoaderState.execution, start->entry, start->stack);
    if (!start->invocation) {
        free(start);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    LxHostThread *thread;
    __atomic_store_n(&GLoaderState.ne_threads_started, 1, __ATOMIC_RELEASE);
    thread = lx_host_thread_create(neThreadEntry, start, neThreadCleanup);
    if (!thread) {
        lx_execution_destroy_thread16(GLoaderState.execution, start->invocation);
        free(start);
        return ERROR_MAX_THRDS_REACHED;
    }
    lx_host_thread_detach(thread);
    *(uint16 *) pTid = (uint16) tid;
    return NO_ERROR;
} // Dos16CreateThread

APIRET16 Dos16GetVersion(PUSHORT pver)
{
    TRACE_NATIVE("Dos16GetVersion(%p)", pver);
    uint8 *bytes = (uint8 *) pver;
    bytes[0] = 45;  // OS/2 Warp 4.52 reports this.
    bytes[1] = 20;
    return NO_ERROR;
} // Dos16GetVersion

APIRET16 Dos16GetMachineMode(PBYTE pmode)
{
    TRACE_NATIVE("Dos16GetMachineMode(%p)", pmode);
    *pmode = 1;  // 1==OS/2 mode (as opposed to zero for DOS mode).
    return NO_ERROR;
} // Dos16GetMachineMode

APIRET16 Dos16GetHugeShift(PUSHORT pcount)
{
    TRACE_NATIVE("Dos16GetHugeShift(%p)", pcount);
    *pcount = 0;  FIXME("maybe?");
    return NO_ERROR;
} // Dos16GetHugeShift

APIRET16 Dos16GetPID(PPIDINFO16 ppidinfo)
{
    TRACE_NATIVE("Dos16GetPID(%p)", ppidinfo);
    PTIB2 tib2 = (PTIB2) LX_GETTIB2();

    // This interface has 16-bit fields even when host IDs are wider.
    ppidinfo->pid = (USHORT) GLoaderState.pib.pib_ulpid;
    ppidinfo->tid = (USHORT) tib2->tib2_ultid;
    ppidinfo->pidParent = (USHORT) GLoaderState.pib.pib_ulppid;
    return NO_ERROR;
} // Dos16GetPID

VOID Dos16Exit(USHORT action, USHORT exitcode)
{
    TRACE_NATIVE("Dos16Exit(%u, %u)", (uint) action, (uint) exitcode);
    DosExit_implementation(action, exitcode);
} // Dos16Exit

APIRET16 Dos16QHandType(USHORT handle, PUSHORT ptype, PUSHORT pflags)
{
    TRACE_NATIVE("Dos16QHandType(%u, %p, %p)", (uint) handle, ptype, pflags);
    ULONG ultype, ulattr;
    const APIRET rc = DosQueryHType_implementation((HFILE) handle, &ultype, &ulattr);
    if (rc == NO_ERROR) {
        if (ptype) *ptype = (USHORT) ultype;
        if (pflags) *pflags = (USHORT) ulattr;
    } // if
    return (APIRET16) rc;
} // Dos16QHandType

APIRET16 Dos16AllocSeg(USHORT size, PUSHORT psel, USHORT flags)
{
    TRACE_NATIVE("Dos16AllocSeg(%u, %p, %u)", (uint) size, psel, (uint) flags);
    if (psel == NULL) {
        return ERROR_INVALID_PARAMETER;
    } else if ((flags & 0xFFF0) != 0) {  // these bits are reserved and must be zero.
        return ERROR_INVALID_PARAMETER;
    }

    // we don't care about the size, we just allocate a whole 64k segment. Oh well.
    uint16 sel = 0xFFFF;
    //void *addr = GLoaderState.allocSegment(&sel, 1);  // we mark these as code in case they want to do self-modifying magic, etc.
    void *addr = GLoaderState.allocSegment(&sel, 0);
    if (!addr) {
        assert(sel == 0xFFFF);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    assert(sel != 0xFFFF);

    if (flags & (1 << 0)) {
        FIXME("segment is meant to be shareable through Dos16GiveSeg");
    }

    if (flags & (1 << 1)) {
        FIXME("segment is meant to be shareable through Dos16GetSeg");
    }

    // documentation says this is a selector (specifically: a segment only on
    //  DOS in the family mode API), but it looks like OS/2 wants a segment too.
    //*psel = (USHORT) sel;
    *psel = (USHORT) ((sel << 3) | 7);
    return NO_ERROR;
} // Dos16AllocSeg

APIRET16 Dos16ReallocSeg(USHORT size, USHORT sel)
{
    TRACE_NATIVE("Dos16ReallocSeg(%u, %u)", (uint) size, (uint) sel);

    // we don't ever discard segments, and we always allocate the full 64k,
    //  so we don't have to worry about resizing an existing segment.
    //  But there are sharing concerns, etc.
    FIXME("this implementation is severely lacking");

    // (we should probably make sure the selector is valid...)
    return NO_ERROR;
} // Dos16ReallocSeg

APIRET16 Dos16FreeSeg(USHORT sel)
{
    TRACE_NATIVE("Dos16FreeSeg(%u)", (uint) sel);
    FIXME("this needs to deal with reference counting and shared segments");
    // documentation says this is a selector (specifically: a segment only on
    //  DOS in the family mode API), but it looks like OS/2 wants a segment too.
    // Do not unmap the main module's own code/data/stack segments while the
    //  app is still running on them (16-bit runtimes free the autodata/stack
    //  at exit and then return through it).
    LxModule *mod = GLoaderState.main_module;
    if (mod) {
        const uint16 alias = (uint16) (sel >> 3);
        for (uint32 i = 0; i < mod->num_mmaps; i++) {
            if (mod->mmaps[i].alias == alias)
                return NO_ERROR;
        } // for
    } // if
    // Selector/segment reuse is deferred for the whole process. In
    //  particular, a CS alias shares its backing pages with the data segment,
    //  so unmapping it here would pull the data out from under the caller.
    (void) sel;
    return NO_ERROR;
} // Dos16FreeSeg

APIRET16 Dos16GetCp(USHORT cb, PUSHORT arCP, PUSHORT pcCP)
{
    TRACE_NATIVE("Dos16GetCp(%u, %p, %p)", (uint) cb, arCP, pcCP);

    FIXME("This is mostly a stub");
    FIXME("...and mostly copy/paste from DosQueryCp()...");

    if (cb < sizeof (*arCP) * 2)
        return ERROR_CPLIST_TOO_SMALL;

    // 437 == United States codepage.
    arCP[0] = 437;  // current process codepage
    arCP[1] = 437;  // prepared system codepage

    if (pcCP)
        *pcCP = sizeof (*arCP) * 2;

    return NO_ERROR;
} // Dos16GetCp

APIRET16 Dos16GetEnv(PUSHORT psel, PUSHORT pcmdoffset)
{
    TRACE_NATIVE("Dos16GetEnv(%p, %p)", psel, pcmdoffset);
    const uint32 cmd1616 = GLoaderState.convert32to1616(GLoaderState.pib.pib_pchcmd);
    if (pcmdoffset) *pcmdoffset = (USHORT) (cmd1616 & 0xFFFF);
    if (psel) *psel = (USHORT) ((cmd1616 >> 16) & 0xFFFF);
    return NO_ERROR;
} // Dos16GetEnv

APIRET16 Dos16Write(USHORT h, PVOID buf, USHORT buflen, PUSHORT actual)
{
    TRACE_NATIVE("Dos16Write(%u, %p, %u, %p)", (uint) h, buf, (uint) buflen, actual);
    ULONG ulactual = actual ? *actual : 0;
    APIRET rc = DosWrite_implementation(h, buf, buflen, &ulactual);
    if (actual) *actual = (USHORT) ulactual;
    return (APIRET16) rc;
} // Dos16Write

APIRET16 Dos16SetVec(USHORT vecnum, PFN routine, PVOID prevaddress)
{
   TRACE_NATIVE("Dos16SetVec(%u, %p, %p)", (uint) vecnum, routine, prevaddress);
   FIXME("do something");
   if (prevaddress) lx_guest_store_u32(prevaddress, 0);
   return NO_ERROR;
} 

APIRET16 Dos16SetSigHandler(PFN routine, PVOID prevaddress, PUSHORT prevaction, USHORT action, USHORT signumber)
{
   TRACE_NATIVE("Dos16SetSigHandler(%p, %p, %p, %u, %u)", routine, prevaddress, prevaction, action, signumber);
   FIXME("do something");
   if (prevaddress) lx_guest_store_u32(prevaddress, 0);
   if (prevaction) *prevaction = 0;
   return NO_ERROR;
} 

APIRET16 Dos16GetInfoSeg(PSEL globalseg, PSEL localseg)
{
    TRACE_NATIVE("Dos16GetInfoSeg(%p, %p)", globalseg, localseg);
    if (getenv("TWOINE_TRACE_SEGMENTS"))
        fprintf(stderr, "2INE InfoSeg: ginfosel=%u(%04X) linfosel=%u(%04X) -> %04X/%04X\n",
                (uint) ginfosel, (uint) (ginfosel << 3 | 7),
                (uint) linfosel, (uint) (linfosel << 3 | 7),
                (uint) (ginfosel << 3 | 7), (uint) (linfosel << 3 | 7));
    if (globalseg) *globalseg = ginfosel << 3 | 7;
    if (localseg) *localseg = linfosel << 3 | 7;
    return NO_ERROR;
}

APIRET16 Dos16QCurDisk(PUSHORT drivenum, PULONG drivemap)
{
    TRACE_NATIVE("Dos16QCurDisk(%p, %p)", drivenum, drivemap);
    ULONG uldrivenum;
    const APIRET rc = DosQueryCurrentDisk_implementation(&uldrivenum, drivemap);
    *drivenum = (USHORT) uldrivenum;
    return (APIRET16) rc;
} // Dos16QCurDisk

APIRET16 Dos16QCurDir(USHORT drivenum, PBYTE dirpath, PUSHORT dirpathlen)
{
    TRACE_NATIVE("Dos16QCurDir(%u, %p, %p)", (uint) drivenum, dirpath, dirpathlen);
    ULONG uldirpathlen = (ULONG) *dirpathlen;
    const APIRET rc = DosQueryCurrentDir_implementation(drivenum, dirpath, &uldirpathlen);
    *dirpathlen = (USHORT) uldirpathlen;
    return (APIRET16) rc;
} // Dos16QCurDir

APIRET16 Dos16HoldSignal(USHORT action)
{
    TRACE_NATIVE("Dos16HoldSignal(%d)", action);
    FIXME("stub");
    return NO_ERROR;
}

APIRET16 Dos16QFsInfo(USHORT drivenum, USHORT fslevel, PBYTE infobuf, USHORT infobufsize)
{
    TRACE_NATIVE("Dos16QFsInfo(%d, %d, %p, %d)", drivenum, fslevel, infobuf, infobufsize);
    const char label[] = "host";
    if (fslevel != 2) return ERROR_INVALID_LEVEL;
    if (infobufsize < 5 + sizeof label) return ERROR_BUFFER_OVERFLOW;
    memset(infobuf, 0, 4); infobuf[4] = sizeof label - 1;
    memcpy(infobuf + 5, label, sizeof label);
    return NO_ERROR;
}

APIRET16 Dos16QFileMode(PCHAR filepath, PUSHORT attr, ULONG res)
{
    TRACE_NATIVE("Dos16QFileMode('%s', %p, %d)", filepath, attr, res);
    APIRET err;
    char *path = makeUnixPath(filepath, &err);
    if (!path) return err;
    LxHostFileInfo info;
    const LxHostFsError error = lx_host_path_info(path, &info); free(path);
    if (error) return fileError(error);
    if (attr) *attr = (info.kind == LX_HOST_FILE_DIRECTORY ? FILE_DIRECTORY : 0) | (info.readonly ? FILE_READONLY : 0);
    return NO_ERROR;
}

APIRET16 Dos16Open(PSZ pszFileName, PUSHORT pHf, PUSHORT pusAction, ULONG cbFile, USHORT ulAttribute, USHORT fsOpenFlags, USHORT fsOpenMode, ULONG ulReserved)
{
    TRACE_NATIVE("Dos16Open('%s', %p, %p, %u, %u, %u, %u, %u)", pszFileName, pHf, pusAction, (uint) cbFile, (uint) ulAttribute, (uint) fsOpenFlags, (uint) fsOpenMode, (uint) ulReserved);
    HFILE hf = (HFILE) -1;
    ULONG action = 0;
    APIRET ret = doDosOpen(pszFileName, &hf, &action, (LONGLONG) cbFile, ulAttribute, fsOpenFlags, fsOpenMode, NULL);
    if (pusAction)
        *pusAction = (USHORT) action;
    if (pHf)
        *pHf = (USHORT) hf;
    TRACE_RET("Dos16Open('%s') -> %u hf=%u action=%u", pszFileName,
              (uint) ret, (uint) hf, (uint) action);
    return (APIRET16) ret;
} // Dos16Open

APIRET16 Dos16FindFirst(PSZ pszFileSpec, PSHORT phdir, USHORT flAttribute, PVOID pfindbuf, USHORT cbBuf, PUSHORT pcFileNames, ULONG res)
{
    if (!phdir || !pcFileNames) return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDir16);
    USHORT id = (USHORT) *phdir;
    ULONG handle;
    if (id == 0xffff) {
        for (id = 2; id < 0xffff && hdir16[id]; id++) {}
        if (id == 0xffff) { ungrabLock(&GMutexDir16); return ERROR_NO_MORE_FILES; }
        handle = HDIR_CREATE;
    } else if (id == HDIR_SYSTEM) handle = HDIR_SYSTEM;
    else if (!hdir16[id]) { ungrabLock(&GMutexDir16); return ERROR_INVALID_HANDLE; }
    else handle = hdir16[id];
    ULONG count = *pcFileNames;
    APIRET result = DosFindFirst(pszFileSpec, &handle, flAttribute, pfindbuf, cbBuf, &count, -1);
    if (!result || result == ERROR_EAS_DIDNT_FIT) {
        if (id != HDIR_SYSTEM) hdir16[id] = handle;
        *phdir = (SHORT) id;
    }
    *pcFileNames = (USHORT) count;
    ungrabLock(&GMutexDir16); return (APIRET16) result;
}

APIRET16 Dos16ChDir(PSZ pszDir, ULONG res)
{
    TRACE_NATIVE("Dos16ChDir('%s', %u)", pszDir, res);
    if (res != 0) {  // reserved, and must be zero, or return error.
        return ERROR_INVALID_PARAMETER;
    }
    return (APIRET16) DosSetCurrentDir_implementation(pszDir);
} // Dos16ChDir

APIRET16 Dos16Close(USHORT hFile)
{
    TRACE_NATIVE("Dos16Close(%u)", (uint) hFile);
    return DosClose_implementation(hFile);
} // Dos16Close

APIRET16 Dos16FindNext(USHORT hDir, PVOID pfindbuf, USHORT cbfindbuf, PUSHORT pcFilenames)
{
    if (!pcFilenames) return ERROR_INVALID_PARAMETER;
    grabLock(&GMutexDir16);
    ULONG handle = hDir == HDIR_SYSTEM ? HDIR_SYSTEM : hdir16[hDir];
    ULONG count = *pcFilenames;
    APIRET result = handle ? DosFindNext(handle, pfindbuf, cbfindbuf, &count) : ERROR_INVALID_HANDLE;
    *pcFilenames = (USHORT) count;
    ungrabLock(&GMutexDir16); return (APIRET16) result;
}

APIRET16 Dos16FindClose(USHORT hDir)
{
    grabLock(&GMutexDir16);
    ULONG handle = hDir == HDIR_SYSTEM ? HDIR_SYSTEM : hdir16[hDir];
    APIRET result = handle ? DosFindClose(handle) : ERROR_INVALID_HANDLE;
    if (!result && hDir != HDIR_SYSTEM) hdir16[hDir] = 0;
    ungrabLock(&GMutexDir16); return (APIRET16) result;
}
 
APIRET16 Dos16Delete(PSZ pszFile, ULONG res)
{
    TRACE_NATIVE("Dos16Delete('%s', %u)", pszFile, (uint) res);
    if (getenv("TWOINE_KEEP_TEMPS"))
        return NO_ERROR;
    return DosDelete_implementation(pszFile);
} // Dos16Delete

APIRET16 Dos16DupHandle(USHORT hFile, PUSHORT pHfile)
{
    TRACE_NATIVE("Dos16DupHandle(%u, %p)", (uint) hFile, pHfile);
    HFILE phf = *(int16_t *)pHfile;
    APIRET ret = DosDupHandle_implementation(hFile, &phf);
    *pHfile = phf;
    return ret;
} // Dos16DupHandle

APIRET16 Dos16Read(USHORT hFile, PVOID pBuffer, USHORT cbRead, PUSHORT pcbActual)
{
    TRACE_NATIVE("Dos16Read(%u, %p, %u, %p)", (uint) hFile, pBuffer, (uint) cbRead, pcbActual);
    ULONG actual;
    APIRET ret = DosRead_implementation(hFile, pBuffer, cbRead, &actual);
    *pcbActual = (USHORT) actual;
    return ret;
} // Dos16Read

APIRET16 Dos16ExecPgm(PCHAR pObjname, SHORT cbObjname, USHORT execFlag, PSZ pArg, PSZ pEnv, PRESULTCODES16 pRes, PSZ pName)
{
    TRACE_NATIVE("Dos16ExecPgm('%s', %d, %u, '%s', '%s', %p, '%s')", pObjname, (int) cbObjname, (uint) execFlag, pArg, pEnv, pRes, pName);
    RESULTCODES res;
    const APIRET ret = DosExecPgm_implementation(pObjname, cbObjname, execFlag, pArg, pEnv, &res, pName);
    pRes->codeTerminate = (USHORT) res.codeTerminate;
    pRes->codeResult = (USHORT) res.codeResult;
    return ret;
} // Dos16ExecPgm

APIRET16 Dos16CWait(USHORT action, USHORT option, PRESULTCODES16 pres, PUSHORT ppid, USHORT pid)
{
    TRACE_NATIVE("Dos16CWait(%u, %u, %p, %p, %u)", (uint) action, (uint) option, pres, ppid, (uint) pid);
    RESULTCODES res;
    PID cpid;
    APIRET ret = DosWaitChild(action, option, &res, &cpid, pid);
    pres->codeTerminate = res.codeTerminate;
    pres->codeResult = res.codeResult;
    *ppid = cpid;
    return ret;
} // Dos16CWait

APIRET16 Dos16ChgFilePtr(USHORT handle, LONG distance, USHORT whence, PULONG newoffset)
{
    TRACE_NATIVE("Dos16ChgFlePtr(%u, %u, %u, %p)", (uint) handle, (uint) distance, (uint) whence, newoffset);
    const APIRET16 ret = (APIRET16) DosSetFilePtr_implementation(handle, distance, whence, newoffset);
    TRACE_RET("Dos16ChgFilePtr(%u, %d, %u) -> %u off=%u", (uint) handle,
              (int) distance, (uint) whence, (uint) ret,
              (uint) (newoffset ? *newoffset : 0));
    return ret;
} // Dos16ChgFilePtr

APIRET16 Dos16Beep(USHORT freq, USHORT duration)
{
    TRACE_NATIVE("Dos16Beep(%u, %u)", freq, duration);
    return DosBeep_implementation(freq, duration);
} // Dos16Beep


APIRET16 Dos16MkDir(PSZ pszDirName, PEAOP2 peaop2)
{
    TRACE_NATIVE("Dos16MkDir('%s', %p)", pszDirName, peaop2);
    return DosCreateDir_implementation(pszDirName, peaop2);
}

APIRET16 Dos16MkDir2(PSZ pszDirName, PEAOP2 peaop2)  // !!! FIXME: check this.
{
    TRACE_NATIVE("Dos16MkDir('%s', %p)", pszDirName, peaop2);
    return DosCreateDir_implementation(pszDirName, peaop2);
}


LX_NATIVE_CONSTRUCTOR(doscalls)
{
    GLoaderState.dosExit = DosExit;
    GLoaderState.writeFile = DosWrite;

    if (!(GMutexDosCalls = lx_host_mutex_create(0, 0))) {
        fprintf(stderr, "Host mutex allocation failed!\n");
        abort();
    } // if

    if (!(GMutexDosBeep = lx_host_mutex_create(0, 0))) {
        lx_host_mutex_destroy(GMutexDosCalls);
        fprintf(stderr, "Host mutex allocation failed!\n");
        abort();
    } // if

    if (!(GMutexDir16 = lx_host_mutex_create(0, 0))) abort();

    MaxHFiles = 20;  // seems to be OS/2's default.
    HFiles = (HFileInfo *) malloc(sizeof (HFileInfo) * MaxHFiles);
    if (!HFiles) {
        lx_host_mutex_destroy(GMutexDosCalls);
        lx_host_mutex_destroy(GMutexDosBeep);
        fprintf(stderr, "Out of memory!\n");
        abort();
    } // if

    HFileInfo *info = HFiles;
    for (uint32 i = 0; i < MaxHFiles; i++, info++) {
        info->handle = 0;
        info->type = 0;
        info->attr = 0;
        info->flags = 0;
    } // for

    // launching a Hello World program from CMD.EXE seems to inherit several
    //  file handles. 0, 1, 2 seem to map to stdin, stdout, stderr (and will
    //  be character devices (maybe CON?) by default, unless you redirect
    //  to a file in which case they're physical files, and using '|' in
    //  CMD.EXE will make handle 1 into a Pipe, of course.
    // Handles, 4, 6 and 9 were also in use (all character devices, attributes
    //  51585, 51392, 51392), but I don't know what these are, if they are
    //  inherited from CMD.EXE or supplied by OS/2 for whatever purpose.
    //  For now, we just wire up stdio.

    FIXME("We can iterate /proc/self/fd to get all currently-open files");
    for (int i = 0; i <= 2; i++) {
        initFileInfo(lx_host_file_standard(i), &HFiles[i], 0);
    } // for

    FIXME("Maybe set this up in lx_loader and make it NULL in lib2ine");
    FIXME("ginfo's time fields update in a timer interrupt!  :O");
    ginfo = (GINFOSEG *) GLoaderState.allocSegment(&ginfosel, 0);
    linfo = (LINFOSEG *) GLoaderState.allocSegment(&linfosel, 0);
    if ((!ginfo) || (!linfo)) {
        lx_host_mutex_destroy(GMutexDosCalls);
        lx_host_mutex_destroy(GMutexDosBeep);
        fprintf(stderr, "Failed to allocate info segments!\n");
        abort();
    }

    memset(ginfo, '\0', sizeof (GINFOSEG));
    memset(linfo, '\0', sizeof (LINFOSEG));
    const uint64 now = lx_host_realtime_us() / 1000000;
    LxHostCalendar value = {0}; lx_host_calendar((int64_t) now, 1, &value);
    ginfo->time = (uint32) now;
    ginfo->hour = value.hour; ginfo->minutes = value.minute; ginfo->seconds = value.second;
    ginfo->timezone = 0; ginfo->cusecTimerInterval = 100;
    ginfo->day = value.day; ginfo->month = value.month;
    ginfo->year = value.year; ginfo->weekday = value.weekday;
    ginfo->uchMajorVersion = 20; FIXME("are these backwards?");
    ginfo->uchMinorVersion = 40; FIXME("are these backwards?");
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

APIRET DosQueryDBCSEnv(ULONG buflen, PCOUNTRYCODE pcc, PCHAR buf)
{
    TRACE_NATIVE("DosQueryDBCSEnv(%u, %p, %p)", (uint) buflen, pcc, buf);
    if ((!pcc) || (!buf))
        return ERROR_INVALID_PARAMETER;
    if ((pcc->country != 0) || (pcc->codepage != 0))
        return ERROR_CODE_PAGE_NOT_FOUND;
    memset(buf, '\0', buflen);
    return NO_ERROR;
} // DosQueryDBCSEnv

APIRET DosSetDefaultDisk(ULONG disknum)
{
    TRACE_NATIVE("DosSetDefaultDisk(%u)", (uint) disknum);

    if ((disknum < 1) || (disknum > 26) || (!GLoaderState.disks[disknum - 1]))
        return ERROR_INVALID_DRIVE;

    GLoaderState.current_disk = disknum;
    return NO_ERROR;
} // DosSetDefaultDisk

APIRET DosDeleteDir(PSZ pszDir)
{
    TRACE_NATIVE("DosDeleteDir('%s')", pszDir ? pszDir : "");

    if (!pszDir)
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *unixpath = makeUnixPath(pszDir, &err);
    if (!unixpath)
        return err;

    const LxHostFsError error = lx_host_path_remove(unixpath, 1);
    free(unixpath);
    return error == LX_HOST_FS_MISSING ? ERROR_PATH_NOT_FOUND : fileError(error);
} // DosDeleteDir

// DosCopy control bits (OS/2 Toolkit bsedos.h).
#define DCPY_EXISTING 0x0001U  // copy even if the target exists.
#define DCPY_APPEND   0x0002U  // append instead of replacing.

APIRET DosCopy(PSZ pszOld, PSZ pszNew, ULONG option)
{
    TRACE_NATIVE("DosCopy('%s', '%s', %u)", pszOld ? pszOld : "",
                 pszNew ? pszNew : "", (uint) option);

    if ((!pszOld) || (!pszNew))
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *oldpath = makeUnixPath(pszOld, &err);
    if (!oldpath)
        return err;
    char *newpath = makeUnixPath(pszNew, &err);
    if (!newpath) {
        free(oldpath);
        return err;
    } // if

    unsigned flags = LX_HOST_FILE_WRITE | LX_HOST_FILE_CREATE;
    if (!(option & (DCPY_EXISTING | DCPY_APPEND))) flags |= LX_HOST_FILE_EXCLUSIVE;
    if (!(option & DCPY_APPEND)) flags |= LX_HOST_FILE_TRUNCATE;
    LxHostFsError error;
    LxHostFile *input = lx_host_file_open(oldpath, LX_HOST_FILE_READ, 0, &error);
    APIRET retval = input ? NO_ERROR : fileError(error);
    if (input) {
        LxHostFile *output = lx_host_file_open(newpath, flags, 0, &error);
        if (!output) retval = fileError(error);
        else {
            if (option & DCPY_APPEND) retval = fileError(lx_host_file_seek(output, 0, 2, NULL));
            char data[16384];
            while (!retval) {
                size_t count;
                error = lx_host_file_read(input, data, sizeof data, &count);
                if (error) { retval = fileError(error); break; }
                if (!count) break;
                size_t written = 0;
                while (written < count) {
                    size_t actual;
                    error = lx_host_file_write(output, data + written, count - written, &actual);
                    if (error || !actual) { retval = error ? fileError(error) : ERROR_DISK_FULL; break; }
                    written += actual;
                }
            }
            lx_host_file_close(output);
        }
        lx_host_file_close(input);
    }

    free(newpath);
    free(oldpath);
    return retval;
} // DosCopy

APIRET DosMove(PSZ pszOld, PSZ pszNew)
{
    TRACE_NATIVE("DosMove('%s', '%s')", pszOld ? pszOld : "",
                 pszNew ? pszNew : "");

    if ((!pszOld) || (!pszNew))
        return ERROR_INVALID_PARAMETER;

    APIRET err = NO_ERROR;
    char *oldpath = makeUnixPath(pszOld, &err);
    if (!oldpath)
        return err;
    char *newpath = makeUnixPath(pszNew, &err);
    if (!newpath) {
        free(oldpath);
        return err;
    } // if

    APIRET retval = fileError(lx_host_path_move(oldpath, newpath));

    free(newpath);
    free(oldpath);
    return retval;
} // DosMove

APIRET DosQueryMem(PVOID pb, PULONG pcb, PULONG pflag)
{
    TRACE_NATIVE("DosQueryMem(%p, %p, %p)", pb, pcb, pflag);

    if ((!pcb) || (!pflag))
        return ERROR_INVALID_PARAMETER;

    // 2ine commits all process memory read/write; report that without
    //  pretending to know allocator boundaries.
    *pflag = PAG_READ | PAG_WRITE | PAG_COMMIT;
    return NO_ERROR;
} // DosQueryMem

APIRET DosSetFileInfo(HFILE hf, ULONG ulInfoLevel, PVOID pInfo, ULONG cbInfoBuf)
{
    TRACE_NATIVE("DosSetFileInfo(%u, %u, %p, %u)", (uint) hf,
                 (uint) ulInfoLevel, pInfo, (uint) cbInfoBuf);

    if (ulInfoLevel != FIL_STANDARD) return ERROR_INVALID_LEVEL;
    if (!pInfo || cbInfoBuf < sizeof(FILESTATUS3)) return ERROR_BUFFER_OVERFLOW;
    LxHandleBorrow file;
    if (!borrowFile(hf, &file)) return ERROR_INVALID_HANDLE;
    const FILESTATUS3 *info = pInfo;
    const LxHostFsError error = lx_host_file_readonly(file.resource, (info->attrFile & FILE_READONLY) != 0);
    lx_handle_release(&file); return fileError(error);
} // DosSetFileInfo

APIRET DosError(ULONG error)
{
    TRACE_NATIVE("DosError(%u)", (uint) error);
    // OS/2 returns the classification for the supplied error number. All of
    //  the shim's errors are "unrecoverable" or ordinary errors; callers use
    //  this mostly for display, so report the ordinary class.
    return ERROR_GEN_FAILURE;
} // DosError

APIRET DosQueryModFromEIP(PHMODULE phmod, PCHAR pszModName, ULONG cbModName,
                          PULONG pulOffset, PULONG pulObject, ULONG ulEIP)
{
    TRACE_NATIVE("DosQueryModFromEIP(%p, %p, %u, %p, %p, %08X)",
                 phmod, pszModName, (uint) cbModName, pulOffset, pulObject,
                 (uint) ulEIP);

    for (LxModule *lxmod = GLoaderState.loaded_modules; lxmod; lxmod = lxmod->next) {
        for (uint32 i = 0; i < lxmod->num_mmaps; i++) {
            const uint32 base = (uint32) (size_t) lxmod->mmaps[i].addr;
            const uint32 end = base + (uint32) lxmod->mmaps[i].size;
            if ((ulEIP >= base) && (ulEIP < end)) {
                if (phmod)
                    *phmod = lxmod ? lxmod->handle : 0;
                if (pulOffset)
                    *pulOffset = ulEIP - base;
                if (pulObject)
                    *pulObject = i + 1;
                if (pszModName) {
                    const size_t len = strlen(lxmod->name);
                    if (len + 1 > cbModName)
                        return ERROR_BUFFER_OVERFLOW;
                    strcpy(pszModName, lxmod->name);
                } // if
                return NO_ERROR;
            } // if
        } // for
    } // for

    return ERROR_FILE_NOT_FOUND;
} // DosQueryModFromEIP

APIRET DosAcknowledgeSignalException(ULONG ulSignalNum)
{
    TRACE_NATIVE("DosAcknowledgeSignalException(%u)", (uint) ulSignalNum);
    return NO_ERROR;
} // DosAcknowledgeSignalException

APIRET DosRaiseException(PEXCEPTIONREPORTRECORD pexcept)
{
    TRACE_NATIVE("DosRaiseException(%p)", pexcept);
    FIXME("OS/2 exception dispatch is not implemented");
    return ERROR_NOT_SUPPORTED;
} // DosRaiseException

APIRET DosUnwindException(PEXCEPTIONREGISTRATIONRECORD phandler,
                          PVOID pTargetIP, PEXCEPTIONREPORTRECORD pERepRec)
{
    TRACE_NATIVE("DosUnwindException(%p, %p, %p)", phandler, pTargetIP, pERepRec);
    FIXME("OS/2 exception unwinding is not implemented");
    return ERROR_NOT_SUPPORTED;
} // DosUnwindException

LX_NATIVE_DESTRUCTOR(doscalls)
{
    GLoaderState.dosExit = NULL;
    GLoaderState.writeFile = NULL;

    ExitListItem *next = GExitList;
    GExitList = NULL;

    for (ExitListItem *item = next; item; item = next) {
        next = item->next;
        free(item);
    } // for

    for (uint32 i = 0; i < MaxHFiles; i++) {
        if (HFiles[i].handle > 1) lx_handle_close(HFiles[i].handle, LX_HANDLE_FILE);
    } // for
    free(HFiles);
    HFiles = NULL;
    MaxHFiles = 0;

    GLoaderState.freeSelector(ginfosel);
    GLoaderState.freeSelector(linfosel);
    lx_host_mutex_destroy(GMutexDosCalls);
    lx_host_mutex_destroy(GMutexDosBeep);
    lx_host_mutex_destroy(GMutexDir16);
}

// end of doscalls.c ...

