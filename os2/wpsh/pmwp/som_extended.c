/* Public domain. No rights reserved. Wholly AI-authored. */
/* Methods that guest code reaches by name through the SOM interface repositories:
   icons, appearance, object semaphores, file attributes, association and class
   manager queries. */
#include "os2/wpsh/pmwp/internal.h"
#include "os2/include/bsedos.h"
#include "os2/include/pmdrag.h"
#include "os2/include/pmshapi.h"
#include "os2/include/pmwin.h"
#include "emulator/abi.h"
#include "host/fs.h"
#include "host/sync.h"
#include "host/thread.h"
#include <ctype.h>
#include <stdio.h>
#include <strings.h>
#include <time.h>

#define ARG(a, n) lx_guest_load_u32((a) + 4 * (n))

enum
{
    PATH_LIMIT = 1024,
    ICON_INFO_BYTES = 28,
    ICON_FORMAT_FILE = 1,
    ICON_FORMAT_RESOURCE = 2,
    ICON_FORMAT_CLEAR = 4,
    ICON_INFO_FORMAT = 4,
    ICON_INFO_FILE = 8,
    ICON_INFO_MODULE = 12,
    ICON_INFO_RESOURCE = 16,
    VIEW_ITEM_VIEW = 8,
    VIEW_ITEM_FRAME = 12,
    VIEW_FLAG_CONTENTS = 1,
    VIEW_FLAG_SETTINGS = 2,
    VIEW_FLAG_HELP = 4,
    VIEW_FLAG_RUNNING = 8,
    VIEW_ANY_FLAGS = -1,
    OPEN_CONTENTS_VIEW = 1,
    OPEN_HELP_VIEW = 3,
    OPEN_RUNNING_VIEW = 4,
    OK_DELETE_CODE = 1,
    NO_DELETE_CODE = 2,
    CANCEL_DELETE_CODE = 3,
    CONFIRM_DELETE_BIT = 1,
    MESSAGE_YES_NO_QUERY = 0x4014,
    MESSAGE_ID_YES = 6,
    ERROR_ACCESS_DENIED_CODE = 5,
    ERROR_TIMEOUT_CODE = 640,
    ERROR_NOT_OWNER_CODE = 288,
    OR_FROMTEMPLATE_CODE = 0x10000004,
    DEFAULT_BUTTON = 3,
    DM_ENDCONVERSATION_MESSAGE = 0x32b,
    DMFL_TARGETFAIL_FLAG = 2,
    RC_DROP_ITEMCOMPLETE_CODE = 1,
    DRAGGED_OVER_REPLY = 0x200000,
    DRAG_ITEM_WINDOW = 0,
    DRAG_ITEM_ID = 4,
    FILE_ATTRIBUTE_READONLY = 1,
    FILE_ATTRIBUTE_DIRECTORY = 0x10,
    FILE_REFRESH_FLAGS = 4,
    FILE_PENDING_NAME = 8,
    FILE_DATES = 0x10,
    FILE_DATE_BYTES = 12,
    FILE_SIZE = 0x1c,
    FILE_EA_SIZE = 0x20,
    FILE_REAL_NAME = 0x24,
    FILE_STATE_KEY = 2,
    FILE_STATE_BYTES = 24,
    WINDOW_KEYS_LIMIT = 260,
    FIND_BUFFER_DATES = 4,
    STATE_PROTECTED_BITS = 0x18,
    WAIT_STEP_MS = 50,
    RECORD_BYTES = 0x20,
    RECORD_TITLE = 0x14,
    RECORD_ICON = 0x18,
    NAMECLASH_NONE_CODE = 1,
    NAMECLASH_CANCEL_CODE = 0,
    CONFIRMATION_DEFAULT = 3,
    QC_NEXT_OPTION = 1,
    LIST_COUNT = 3,
    SHADOW_LIMIT = 1024
};

static uint32 class_of(uint32 self)
{
    return pmwpInvokeObject(PMWP_SOMObject_somGetClass, self, 0, 0, 0);
}

static ULONG handle_of(uint32 self)
{
    return pmwpObjectField(self, PMWP_OBJ_HANDLE);
}

static uint32 wpSetError(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_ERROR, ARG(a, 1));
    return TRUE;
}

static uint32 wpQueryError(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_ERROR);
}

static uint32 wpSetIcon(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_ICON, ARG(a, 1));
    return TRUE;
}

static uint32 wpQueryIcon(uint8 *a)
{
    uint32 self = ARG(a, 0), icon = pmwpObjectField(self, PMWP_OBJ_ICON);
    return icon ? icon
                : pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsQueryIcon, class_of(self), 0, 0,
                             0);
}

/* Loads the pointer an ICONINFO describes. Inline icon data is not decoded. */
static BOOL load_icon(uint32 info, uint32 *pointer)
{
    uint32 format = pmwpLoad32(info + ICON_INFO_FORMAT);
    if (format == ICON_FORMAT_CLEAR)
    {
        *pointer = 0;
        return TRUE;
    }
    if (format == ICON_FORMAT_RESOURCE)
        *pointer = WinLoadPointer(HWND_DESKTOP, pmwpLoad32(info + ICON_INFO_MODULE),
                                  pmwpLoad32(info + ICON_INFO_RESOURCE));
    else if (format == ICON_FORMAT_FILE)
    {
        char *name = lx_execution_copy_string(GProcessState.execution,
                                              pmwpLoad32(info + ICON_INFO_FILE), PATH_LIMIT);
        *pointer = name ? WinLoadFileIcon(name, FALSE) : 0;
        free(name);
    }
    else
        return FALSE;
    return *pointer != 0;
}

/* The description is kept so wpQueryIconData can report what was set. */
static uint32 wpSetIconData(uint8 *a)
{
    uint32 self = ARG(a, 0), info = ARG(a, 1), pointer = 0;
    if (!info || !load_icon(info, &pointer))
        return FALSE;
    uint32 format = pmwpLoad32(info + ICON_INFO_FORMAT);
    pmwpFree(pmwpObjectField(self, PMWP_OBJ_ICON_FILE));
    pmwpSetObjectField(self, PMWP_OBJ_ICON_FILE, 0);
    if (format == ICON_FORMAT_FILE)
    {
        char *name = lx_execution_copy_string(GProcessState.execution,
                                              pmwpLoad32(info + ICON_INFO_FILE), PATH_LIMIT);
        pmwpSetObjectField(self, PMWP_OBJ_ICON_FILE, name ? pmwpGuestString(name) : 0);
        free(name);
    }
    pmwpSetObjectField(self, PMWP_OBJ_ICON_FORMAT, format);
    pmwpSetObjectField(self, PMWP_OBJ_ICON_MODULE, pmwpLoad32(info + ICON_INFO_MODULE));
    pmwpSetObjectField(self, PMWP_OBJ_ICON_RESOURCE, pmwpLoad32(info + ICON_INFO_RESOURCE));
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetIcon, self, pointer, 0, 0);
}

/* A default icon is described as cleared; a null buffer asks for the required size. */
static uint32 report_cleared_icon(uint32 info)
{
    if (info)
    {
        pmwpStore32(info, ICON_INFO_BYTES);
        pmwpStore32(info + ICON_INFO_FORMAT, ICON_FORMAT_CLEAR);
    }
    return ICON_INFO_BYTES;
}

static uint32 wpclsQueryIconData(uint8 *a)
{
    return report_cleared_icon(ARG(a, 1));
}

/* A file icon's name follows the ICONINFO in the caller's buffer. */
static uint32 wpQueryIconData(uint8 *a)
{
    uint32 self = ARG(a, 0), info = ARG(a, 1);
    uint32 format = pmwpObjectField(self, PMWP_OBJ_ICON_FORMAT);
    if (!format || format == ICON_FORMAT_CLEAR)
        return report_cleared_icon(info);
    char *name = NULL;
    uint32 size = ICON_INFO_BYTES;
    if (format == ICON_FORMAT_FILE)
    {
        name = lx_execution_copy_string(GProcessState.execution,
                                        pmwpObjectField(self, PMWP_OBJ_ICON_FILE), PATH_LIMIT);
        if (!name)
            return 0;
        size += (uint32)strlen(name) + 1;
    }
    if (info)
    {
        pmwpStore32(info, ICON_INFO_BYTES);
        pmwpStore32(info + ICON_INFO_FORMAT, format);
        pmwpStore32(info + ICON_INFO_MODULE, pmwpObjectField(self, PMWP_OBJ_ICON_MODULE));
        pmwpStore32(info + ICON_INFO_RESOURCE, pmwpObjectField(self, PMWP_OBJ_ICON_RESOURCE));
        if (name)
        {
            pmwpStore32(info + ICON_INFO_FILE, info + ICON_INFO_BYTES);
            lx_execution_write(GProcessState.execution, info + ICON_INFO_BYTES, name,
                               strlen(name) + 1);
        }
    }
    free(name);
    return size;
}

static uint32 wpclsSetIcon(uint8 *a)
{
    pmwpSetClassField(ARG(a, 0), PMWP_CLS_ICON, ARG(a, 1));
    return TRUE;
}

static uint32 wpclsSetIconData(uint8 *a)
{
    uint32 pointer = 0;
    if (!ARG(a, 1) || !load_icon(ARG(a, 1), &pointer))
        return FALSE;
    pmwpSetClassField(ARG(a, 0), PMWP_CLS_ICON, pointer);
    return TRUE;
}

static uint32 wpclsSetSettingsPageSize(uint8 *a)
{
    uint32 self = ARG(a, 0), size = ARG(a, 1);
    if (!size)
        return FALSE;
    pmwpSetClassField(self, PMWP_CLS_PAGE_WIDTH, pmwpLoad32(size));
    pmwpSetClassField(self, PMWP_CLS_PAGE_HEIGHT, pmwpLoad32(size + 4));
    return TRUE;
}

static uint32 wpclsQueryButtonAppearance(uint8 *a)
{
    (void)a;
    return DEFAULT_BUTTON;
}

static uint32 wpQueryButtonAppearance(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_BUTTON);
}

static uint32 wpSetButtonAppearance(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_BUTTON, ARG(a, 1));
    return 0;
}

static uint32 wpQueryMinWindow(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_MIN_WINDOW);
}

static uint32 wpSetMinWindow(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_MIN_WINDOW, ARG(a, 1));
    return 0;
}

static uint32 wpSetDefaultIconPos(uint8 *a)
{
    uint32 self = ARG(a, 0), point = ARG(a, 1);
    if (!point)
        return FALSE;
    pmwpSetObjectField(self, PMWP_OBJ_ICON_X, pmwpLoad32(point));
    pmwpSetObjectField(self, PMWP_OBJ_ICON_Y, pmwpLoad32(point + 4));
    return TRUE;
}

static uint32 wpQueryDefaultIconPos(uint8 *a)
{
    uint32 self = ARG(a, 0), point = ARG(a, 1);
    return point && pmwpStore32(point, pmwpObjectField(self, PMWP_OBJ_ICON_X)) &&
           pmwpStore32(point + 4, pmwpObjectField(self, PMWP_OBJ_ICON_Y));
}

static uint32 wpIsObjectInitialized(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_TITLE) != 0;
}

static void show_views(uint32 self, BOOL show)
{
    for (uint32 item = pmwpObjectField(self, PMWP_OBJ_USE_LIST); item; item = pmwpLoad32(item + 4))
        if (pmwpLoad32(item) == PMWP_USAGE_OPENVIEW)
            WinShowWindow(pmwpLoad32(item + VIEW_ITEM_FRAME), show);
}

static uint32 wpHide(uint8 *a)
{
    show_views(ARG(a, 0), FALSE);
    return TRUE;
}

static uint32 wpRestore(uint8 *a)
{
    show_views(ARG(a, 0), TRUE);
    return TRUE;
}

static uint32 view_flag(uint32 view)
{
    switch (view)
    {
        case OPEN_CONTENTS_VIEW:
            return VIEW_FLAG_CONTENTS;
        case PMWP_OPEN_SETTINGS:
            return VIEW_FLAG_SETTINGS;
        case OPEN_HELP_VIEW:
            return VIEW_FLAG_HELP;
        case OPEN_RUNNING_VIEW:
            return VIEW_FLAG_RUNNING;
        default:
            return 0;
    }
}

/* The current item and result address the VIEWITEM that follows each USEITEM. */
static uint32 wpFindViewItem(uint8 *a)
{
    uint32 flags = ARG(a, 1), current = ARG(a, 2);
    uint32 item = current ? pmwpLoad32(current - VIEW_ITEM_VIEW + 4)
                          : pmwpObjectField(ARG(a, 0), PMWP_OBJ_USE_LIST);
    for (; item; item = pmwpLoad32(item + 4))
        if (pmwpLoad32(item) == PMWP_USAGE_OPENVIEW &&
            (flags == (uint32)VIEW_ANY_FLAGS ||
             flags & view_flag(pmwpLoad32(item + VIEW_ITEM_VIEW))))
            return item + VIEW_ITEM_VIEW;
    return 0;
}

static uint32 wpCreateAnother(uint8 *a)
{
    uint32 self = ARG(a, 0);
    return pmwpNewObject(class_of(self), ARG(a, 1), ARG(a, 2), ARG(a, 3), FALSE);
}

static uint32 wpCreateFromTemplate(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint32 copy =
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpCopyObject, self, ARG(a, 1), ARG(a, 2), 0);
    if (!copy)
        return 0;
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpModifyStyle, copy, PMWP_OBJSTYLE_TEMPLATE, 0, 0);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpObjectReady, copy, OR_FROMTEMPLATE_CODE, self, 0);
    return copy;
}

static uint32 wpConfirmDelete(uint8 *a)
{
    uint32 self = ARG(a, 0);
    if (pmwpObjectField(self, PMWP_OBJ_STYLE) & PMWP_OBJSTYLE_NODELETE)
        return NO_DELETE_CODE;
    if (!(ARG(a, 1) & CONFIRM_DELETE_BIT))
        return OK_DELETE_CODE;
    char *title = lx_execution_copy_string(GProcessState.execution,
                                           pmwpObjectField(self, PMWP_OBJ_TITLE), 4097);
    ULONG reply = WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, "Delete this object?",
                                title ? title : "Delete", 0, MESSAGE_YES_NO_QUERY);
    free(title);
    return reply == MESSAGE_ID_YES ? OK_DELETE_CODE : CANCEL_DELETE_CODE;
}

/* Each object has an owner and a nesting count. The monitor guards both, and a waiter
   sleeps on it for at most the requested time. */
static LxHostOnce semaphore_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *semaphore_lock;

static void initialize_semaphores(void)
{
    semaphore_lock = lx_host_monitor_create();
    if (!semaphore_lock)
        abort();
}

static uint32 current_owner(void)
{
    return (uint32)lx_host_thread_id() | 1;
}

static uint32 wpRequestObjectMutexSem(uint8 *a)
{
    lx_host_once(&semaphore_once, initialize_semaphores);
    uint32 data = pmwpInstanceData(PMWP_WPOBJECT, ARG(a, 0)), owner = current_owner();
    uint32 result = NO_ERROR;
    lx_host_monitor_lock(semaphore_lock);
    for (uint32 holder = pmwpLoad32(data + PMWP_OBJ_MUTEX_OWNER); holder && holder != owner;
         holder = pmwpLoad32(data + PMWP_OBJ_MUTEX_OWNER))
        if (lx_host_monitor_wait(semaphore_lock, ARG(a, 1)) != LX_HOST_LOCK_OK)
        {
            result = ERROR_TIMEOUT_CODE;
            break;
        }
    if (result == NO_ERROR)
    {
        pmwpStore32(data + PMWP_OBJ_MUTEX_OWNER, owner);
        pmwpStore32(data + PMWP_OBJ_MUTEX_DEPTH, pmwpLoad32(data + PMWP_OBJ_MUTEX_DEPTH) + 1);
    }
    lx_host_monitor_unlock(semaphore_lock);
    return result;
}

static uint32 wpReleaseObjectMutexSem(uint8 *a)
{
    lx_host_once(&semaphore_once, initialize_semaphores);
    uint32 data = pmwpInstanceData(PMWP_WPOBJECT, ARG(a, 0)), result = NO_ERROR;
    lx_host_monitor_lock(semaphore_lock);
    if (pmwpLoad32(data + PMWP_OBJ_MUTEX_OWNER) != current_owner())
        result = ERROR_NOT_OWNER_CODE;
    else
    {
        uint32 depth = pmwpLoad32(data + PMWP_OBJ_MUTEX_DEPTH) - 1;
        pmwpStore32(data + PMWP_OBJ_MUTEX_DEPTH, depth);
        if (!depth)
        {
            pmwpStore32(data + PMWP_OBJ_MUTEX_OWNER, 0);
            lx_host_monitor_wake_all(semaphore_lock);
        }
    }
    lx_host_monitor_unlock(semaphore_lock);
    return result;
}

static uint32 wpAssertObjectMutexSem(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_MUTEX_OWNER) == current_owner();
}

/* The base object accepts no drop: it ends the conversation as a target failure. */
static uint32 wpDrop(uint8 *a)
{
    uint32 item = ARG(a, 3);
    DrgSendTransferMsg(pmwpLoad32(item + DRAG_ITEM_WINDOW), DM_ENDCONVERSATION_MESSAGE,
                       (MPARAM)(uintptr_t)pmwpLoad32(item + DRAG_ITEM_ID),
                       (MPARAM)(uintptr_t)DMFL_TARGETFAIL_FLAG);
    return RC_DROP_ITEMCOMPLETE_CODE;
}

static uint32 wpDraggedOverObject(uint8 *a)
{
    (void)a;
    return DRAGGED_OVER_REPLY;
}

static BOOL file_path(uint32 self, char *path, size_t capacity)
{
    return handle_of(self) && pmwpStorePath(handle_of(self), path, capacity);
}

static BOOL file_info(uint32 self, LxHostFileInfo *info)
{
    char path[PATH_LIMIT];
    if (!file_path(self, path, sizeof path))
        return FALSE;
    char *host = pmwpHostPath(path);
    BOOL result = host && lx_host_path_info(host, info) == LX_HOST_FS_OK;
    free(host);
    return result;
}

static uint32 wpQueryFileSize(uint8 *a)
{
    LxHostFileInfo info;
    return file_info(ARG(a, 0), &info) ? (uint32)info.size : 0;
}

static uint32 wpQueryAttr(uint8 *a)
{
    LxHostFileInfo info;
    if (!file_info(ARG(a, 0), &info))
        return 0;
    return (info.readonly ? FILE_ATTRIBUTE_READONLY : 0) |
           (info.kind == LX_HOST_FILE_DIRECTORY ? FILE_ATTRIBUTE_DIRECTORY : 0);
}

static uint32 wpSetAttr(uint8 *a)
{
    char path[PATH_LIMIT];
    if (!file_path(ARG(a, 0), path, sizeof path))
        return FALSE;
    char *host = pmwpHostPath(path);
    BOOL result =
        host && lx_host_path_readonly(host, ARG(a, 1) & FILE_ATTRIBUTE_READONLY) == LX_HOST_FS_OK;
    free(host);
    return result;
}

/* FDATE and FTIME are packed FAT-style words in local time. */
static uint32 store_file_time(int64_t when, uint32 date_address, uint32 time_address)
{
    time_t seconds = (time_t)when;
    struct tm parts;
    if (!localtime_r(&seconds, &parts))
        return ERROR_ACCESS_DENIED_CODE;
    uint8 date[2], clock[2];
    int year = parts.tm_year - 80;
    lx_guest_store_u16(
        date, (uint16)((year < 0 ? 0 : year) << 9 | (parts.tm_mon + 1) << 5 | parts.tm_mday));
    lx_guest_store_u16(clock, (uint16)(parts.tm_hour << 11 | parts.tm_min << 5 | parts.tm_sec / 2));
    if ((date_address && !lx_execution_write(GProcessState.execution, date_address, date, 2)) ||
        (time_address && !lx_execution_write(GProcessState.execution, time_address, clock, 2)))
        return ERROR_ACCESS_DENIED_CODE;
    return NO_ERROR;
}

static uint32 wpQueryLastWrite(uint8 *a)
{
    LxHostFileInfo info;
    return file_info(ARG(a, 0), &info) ? store_file_time(info.modified, ARG(a, 1), ARG(a, 2))
                                       : ERROR_ACCESS_DENIED_CODE;
}

static uint32 wpQueryLastAccess(uint8 *a)
{
    LxHostFileInfo info;
    return file_info(ARG(a, 0), &info) ? store_file_time(info.accessed, ARG(a, 1), ARG(a, 2))
                                       : ERROR_ACCESS_DENIED_CODE;
}

static uint32 wpVerifyUpdateAccess(uint8 *a)
{
    LxHostFileInfo info;
    return file_info(ARG(a, 0), &info) && !info.readonly ? NO_ERROR : ERROR_ACCESS_DENIED_CODE;
}

static uint32 wpSetRealName(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char path[PATH_LIMIT], target[PATH_LIMIT + 300], setup[PATH_LIMIT + 320];
    char *name = lx_execution_copy_string(GProcessState.execution, ARG(a, 1), 256);
    BOOL result = FALSE;
    if (name && *name && !strpbrk(name, "\\/") && file_path(self, path, sizeof path))
    {
        char *slash = strrchr(path, '\\');
        size_t directory = slash ? (size_t)(slash - path) + 1 : 0;
        snprintf(target, sizeof target, "%.*s%s", (int)directory, path, name);
        char *from = pmwpHostPath(path), *to = pmwpHostPath(target);
        if (from && to && lx_host_path_move(from, to) == LX_HOST_FS_OK)
        {
            snprintf(setup, sizeof setup, "PATH=%s;", target);
            result = pmwpStoreAppendSetup(handle_of(self), setup);
        }
        free(from);
        free(to);
    }
    free(name);
    return result;
}

static uint32 wpSetTitleAndRenameFile(uint8 *a)
{
    uint32 self = ARG(a, 0), title = ARG(a, 1);
    return pmwpInvoke(PMWP_WPFILESYSTEM, PMWP_WPFileSystem_wpSetRealName, self, title, 0, 0) &&
           pmwpObjectSetTitle(self, title);
}

static uint32 wpQueryRefreshFlags(uint8 *a)
{
    return pmwpLoad32(pmwpInstanceData(PMWP_WPFILESYSTEM, ARG(a, 0)) + FILE_REFRESH_FLAGS);
}

static uint32 wpSetRefreshFlags(uint8 *a)
{
    return pmwpStore32(pmwpInstanceData(PMWP_WPFILESYSTEM, ARG(a, 0)) + FILE_REFRESH_FLAGS,
                       ARG(a, 1));
}

/* Host drives are fixed for the life of a process. */
static uint32 wpIsDiskSwapped(uint8 *a)
{
    (void)a;
    return FALSE;
}

static uint32 wpclsQueryAwakeObject(uint8 *a)
{
    char *path = lx_execution_copy_string(GProcessState.execution, ARG(a, 1), PATH_LIMIT);
    size_t count = 0;
    ULONG *handles = path ? pmwpStoreList(&count) : NULL;
    uint32 result = 0;
    for (size_t i = 0; i < count && !result; i++)
    {
        PmwpRecord record = {0};
        char stored[PATH_LIMIT];
        if (!pmwpStoreLoad(handles[i], &record))
            continue;
        if (pmwpSetupValue(record.setup, "PATH", stored, sizeof stored) &&
            !strcasecmp(stored, path))
            result = pmwpAwakeObject(handles[i]);
        pmwpStoreRelease(&record);
    }
    free(handles);
    free(path);
    return result;
}

static uint32 wpclsFileSysExists(uint8 *a)
{
    char folder[PATH_LIMIT], full[PATH_LIMIT + 260];
    uint32 parent = ARG(a, 1);
    char *name = lx_execution_copy_string(GProcessState.execution, ARG(a, 2), 256);
    uint32 result = 0;
    if (name && parent && file_path(parent, folder, sizeof folder))
    {
        size_t length = strlen(folder);
        snprintf(full, sizeof full, "%s%s%s", folder,
                 length && folder[length - 1] == '\\' ? "" : "\\", name);
        int kind = pmwpPathKind(full);
        if (kind && !!(ARG(a, 3) & FILE_ATTRIBUTE_DIRECTORY) == (kind == 2))
            result = pmwpObjectForPath(full);
    }
    free(name);
    return result;
}

static uint32 wpModuleForClass(uint8 *a)
{
    char *name = lx_execution_copy_string(GProcessState.execution, ARG(a, 1), 256);
    char module[260];
    uint32 result = 0;
    if (name)
    {
        for (int id = 0; id < PMWP_CLASS_COUNT && !result; id++)
            if (!strcmp(pmwpClassRows[id].name, name))
                result = pmwpConstantString("PMWP");
        if (!result && pmwpStoreClassModule(name, module, sizeof module))
            result = pmwpConstantString(module);
    }
    free(name);
    return result;
}

static uint32 wpReplacementIsInEffect(uint8 *a)
{
    char *from = lx_execution_copy_string(GProcessState.execution, ARG(a, 1), 256);
    char *to = lx_execution_copy_string(GProcessState.execution, ARG(a, 2), 256);
    char replacement[256];
    BOOL result = from && to && pmwpStoreReplacement(from, replacement, sizeof replacement) &&
                  !strcmp(replacement, to);
    free(from);
    free(to);
    return result;
}

static uint32 wpGetTrueClassName(uint8 *a)
{
    return pmwpInvokeClass(PMWP_SOMClass_somGetName, class_of(ARG(a, 1)), 0, 0, 0);
}

#define METHOD_VALUE(name, value)                                                                  \
    static uint32 name(uint8 *a)                                                                   \
    {                                                                                              \
        (void)a;                                                                                   \
        return value;                                                                              \
    }
/* Base results of PMWP.DLL: these methods exist for subclasses to override. */
METHOD_VALUE(base_zero, 0)
METHOD_VALUE(base_true, TRUE)
METHOD_VALUE(name_clash_options, 0x20)
METHOD_VALUE(keep_rename_default, 2)
METHOD_VALUE(confirmations_default, CONFIRMATION_DEFAULT)

static uint32 wpQueryState(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_STATE);
}

static uint32 wpSetState(uint8 *a)
{
    uint32 self = ARG(a, 0), old = pmwpObjectField(self, PMWP_OBJ_STATE);
    pmwpSetObjectField(self, PMWP_OBJ_STATE,
                       (ARG(a, 1) & ~(uint32)STATE_PROTECTED_BITS) | (old & STATE_PROTECTED_BITS));
    return TRUE;
}

static uint32 wpModifyState(uint8 *a)
{
    uint32 self = ARG(a, 0), mask = ARG(a, 1), old = pmwpObjectField(self, PMWP_OBJ_STATE);
    pmwpSetObjectField(self, PMWP_OBJ_STATE, (old & ~mask) | (ARG(a, 2) & mask));
    return TRUE;
}

static uint32 wpSetNextObj(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_NEXT, ARG(a, 1));
    return TRUE;
}

/* Inside a folder the next object is the folder's next content entry. */
static uint32 wpQueryNextObj(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint32 folder = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryFolder, self, 0, 0, 0);
    return folder ? pmwpInvoke(PMWP_WPFOLDER, PMWP_WPFolder_wpQueryContent, folder, self,
                               QC_NEXT_OPTION, 0)
                  : pmwpObjectField(self, PMWP_OBJ_NEXT);
}

static uint32 get_next_pointer(uint8 *a)
{
    return pmwpInstanceData(PMWP_WPOBJECT, ARG(a, 0)) + PMWP_OBJ_NEXT;
}

static uint32 wpSetInsertState(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_INSERT_STATE, ARG(a, 1));
    return 0;
}

static uint32 wpQueryIconSaved(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_ICON);
}

static uint32 wpQueryContainer(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryFolder, ARG(a, 0), 0, 0, 0);
}

static uint32 wpSetContainer(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetFolder, ARG(a, 0), ARG(a, 1), 0, 0);
}

static uint32 wpQueryContainerFlagPtr(uint8 *a)
{
    return pmwpInstanceData(PMWP_WPOBJECT, ARG(a, 0)) + PMWP_OBJ_CONTAINER_FLAGS;
}

/* The record exists for callers that read its title and icon; no container shows it. */
static uint32 wpQueryCoreRecord(uint8 *a)
{
    uint32 self = ARG(a, 0), record = pmwpObjectField(self, PMWP_OBJ_RECORD);
    if (!record)
    {
        record = pmwpCalloc(RECORD_BYTES);
        pmwpSetObjectField(self, PMWP_OBJ_RECORD, record);
    }
    if (record)
    {
        pmwpStore32(record, RECORD_BYTES);
        pmwpStore32(record + RECORD_TITLE, pmwpObjectField(self, PMWP_OBJ_TITLE));
        pmwpStore32(record + RECORD_ICON,
                    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryIcon, self, 0, 0, 0));
    }
    return record;
}

static uint32 wpQueryScreenGroupID(uint8 *a)
{
    return ARG(a, 1) & 0xffff;
}

static uint32 wpIdentify(uint8 *a)
{
    char *name = lx_execution_copy_string(
        GProcessState.execution,
        pmwpInvokeClass(PMWP_SOMClass_somGetName, class_of(ARG(a, 0)), 0, 0, 0), 256);
    char text[260];
    BOOL result = FALSE;
    if (name && ARG(a, 1))
    {
        snprintf(text, sizeof text, "%s:", name);
        result = lx_execution_write(GProcessState.execution, ARG(a, 1), text, strlen(text) + 1);
    }
    free(name);
    return result;
}

/* Each saved value reaches the store when it is appended, so a save needs no buffer and
   the begin and end calls only bracket it. */
static uint32 wpBeginRestore(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_RESTORING, 1);
    return TRUE;
}

static uint32 wpEndRestore(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_RESTORING, 0);
    return 0;
}

static uint32 wpAppendSaveData(uint8 *a)
{
    const uint32 words[] = {ARG(a, 0), ARG(a, 1), ARG(a, 2), ARG(a, 4), ARG(a, 5)};
    return pmwpGuestCallMany(pmwpClassToken(PMWP_WPOBJECT, PMWP_WPObject_wpSaveData), words, 5);
}

static uint32 wpExtractRestoreData(uint8 *a)
{
    const uint32 words[] = {ARG(a, 0), ARG(a, 1), ARG(a, 2), ARG(a, 4), ARG(a, 5)};
    return pmwpGuestCallMany(pmwpClassToken(PMWP_WPOBJECT, PMWP_WPObject_wpRestoreData), words, 5);
}

/* A dormant object saves and leaves memory; its record stays. */
static uint32 wpMakeDormant(uint8 *a)
{
    uint32 self = ARG(a, 0);
    if (pmwpObjectField(self, PMWP_OBJ_LOCKS))
        return FALSE;
    if (ARG(a, 1))
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSaveImmediate, self, 0, 0, 0);
    pmwpInvokeObject(PMWP_SOMObject_somFree, self, 0, 0, 0);
    return TRUE;
}

/* The target leaves its folder and a copy of this object takes its place. */
static uint32 wpReplaceObject(uint8 *a)
{
    uint32 self = ARG(a, 0), target = ARG(a, 1);
    uint32 folder = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryFolder, target, 0, 0, 0);
    if (pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpDelete, target, 0, 0, 0) != OK_DELETE_CODE ||
        !pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpCopyObject, self, folder, TRUE, 0))
        return FALSE;
    if (ARG(a, 2))
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpDelete, self, 0, 0, 0);
    return TRUE;
}

static uint32 wpConfirmObjectTitle(uint8 *a)
{
    uint32 self = ARG(a, 0), folder = ARG(a, 1), duplicate_out = ARG(a, 2);
    char *title = lx_execution_copy_string(GProcessState.execution, ARG(a, 3), 4097);
    uint32 result = NAMECLASH_NONE_CODE;
    for (uint32 child = title && folder ? pmwpInvoke(PMWP_WPFOLDER, PMWP_WPFolder_wpQueryContent,
                                                     folder, 0, 0, 0)
                                        : 0;
         child; child = pmwpInvoke(PMWP_WPFOLDER, PMWP_WPFolder_wpQueryContent, folder, child,
                                   QC_NEXT_OPTION, 0))
    {
        char *other = lx_execution_copy_string(GProcessState.execution,
                                               pmwpObjectField(child, PMWP_OBJ_TITLE), 4097);
        BOOL clash = other && child != self && !strcasecmp(other, title);
        free(other);
        if (clash)
        {
            if (duplicate_out)
                pmwpStore32(duplicate_out, child);
            result = NAMECLASH_CANCEL_CODE;
            break;
        }
    }
    free(title);
    return result;
}

/* Polls the object's views. A negative timeout waits indefinitely. */
static uint32 wpWaitForClose(uint8 *a)
{
    uint32 self = ARG(a, 0), view = ARG(a, 1);
    LONG remaining = (LONG)ARG(a, 3);
    HAB hab = WinQueryAnchorBlock(HWND_DESKTOP);
    for (BOOL asked = FALSE;; asked = TRUE)
    {
        BOOL open = FALSE;
        for (uint32 item = pmwpObjectField(self, PMWP_OBJ_USE_LIST); item;
             item = pmwpLoad32(item + 4))
        {
            HWND frame = pmwpLoad32(item + VIEW_ITEM_FRAME);
            if (pmwpLoad32(item) == PMWP_USAGE_OPENVIEW && (!view || frame == view) &&
                WinIsWindow(hab, frame))
            {
                open = TRUE;
                if (ARG(a, 4) && !asked)
                    WinPostMsg(frame, WM_CLOSE, 0, 0);
            }
        }
        if (!open)
            return NO_ERROR;
        if (!remaining)
            return ERROR_TIMEOUT_CODE;
        DosSleep(WAIT_STEP_MS);
        if (remaining > 0)
            remaining = remaining > WAIT_STEP_MS ? remaining - WAIT_STEP_MS : 0;
    }
}

/* wpclsLockDirtyList and its peers guard three process-wide lists; the owning thread
   may nest. They use the semaphore monitor. */
static uint32 list_owner[LIST_COUNT], list_depth[LIST_COUNT];

static uint32 lock_list(unsigned list)
{
    lx_host_once(&semaphore_once, initialize_semaphores);
    uint32 owner = current_owner();
    lx_host_monitor_lock(semaphore_lock);
    while (list_owner[list] && list_owner[list] != owner)
        lx_host_monitor_wait(semaphore_lock, UINT32_MAX);
    list_owner[list] = owner;
    list_depth[list]++;
    lx_host_monitor_unlock(semaphore_lock);
    return TRUE;
}

static uint32 unlock_list(unsigned list)
{
    lx_host_once(&semaphore_once, initialize_semaphores);
    lx_host_monitor_lock(semaphore_lock);
    BOOL owned = list_owner[list] == current_owner();
    if (owned && !--list_depth[list])
    {
        list_owner[list] = 0;
        lx_host_monitor_wake_all(semaphore_lock);
    }
    lx_host_monitor_unlock(semaphore_lock);
    return owned;
}

#define LIST_LOCK(name, list)                                                                      \
    static uint32 lock_##name(uint8 *a)                                                            \
    {                                                                                              \
        (void)a;                                                                                   \
        return lock_list(list);                                                                    \
    }                                                                                              \
    static uint32 unlock_##name(uint8 *a)                                                          \
    {                                                                                              \
        (void)a;                                                                                   \
        return unlock_list(list);                                                                  \
    }
LIST_LOCK(dirty, 0)
LIST_LOCK(sleepy, 1)
LIST_LOCK(restart, 2)

/* wpSaveDeferred saves at once, so no object is ever waiting to be saved. */
static uint32 wpclsSaveDirtyObjects(uint8 *a)
{
    (void)a;
    return TRUE;
}

static uint32 wpclsMakeAwake(uint8 *a)
{
    uint32 self = ARG(a, 0), object = pmwpAllocateObject(self);
    if (!object)
        return 0;
    pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsIncUsage, self, 0, 0, 0);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetFolder, object, ARG(a, 5), 0, 0);
    pmwpObjectSetTitle(object, ARG(a, 1));
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetStyle, object, ARG(a, 2), 0, 0);
    if (ARG(a, 3))
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetIcon, object, ARG(a, 3), 0, 0);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpObjectReady, object, PMWP_OR_AWAKE, 0, 0);
    return object;
}

/* FDATE and FTIME words kept for creation, last access and last write, in that order.
   The host reports no creation time, so an uncached creation date is the write date. */
static BOOL load_dates(uint32 self, uint8 dates[FILE_DATE_BYTES])
{
    uint32 data = pmwpInstanceData(PMWP_WPFILESYSTEM, self) + FILE_DATES;
    if (!lx_execution_read(GProcessState.execution, data, dates, FILE_DATE_BYTES))
        return FALSE;
    for (unsigned i = 0; i < FILE_DATE_BYTES; i++)
        if (dates[i])
            return TRUE;
    LxHostFileInfo info;
    if (!file_info(self, &info))
        return FALSE;
    uint32 scratch = pmwpMalloc(4);
    if (!scratch)
        return FALSE;
    store_file_time(info.modified, scratch, scratch + 2);
    lx_execution_read(GProcessState.execution, scratch, dates, 4);
    memcpy(dates + 8, dates, 4);
    store_file_time(info.accessed, scratch, scratch + 2);
    lx_execution_read(GProcessState.execution, scratch, dates + 4, 4);
    pmwpFree(scratch);
    return lx_execution_write(GProcessState.execution, data, dates, FILE_DATE_BYTES);
}

static uint32 wpQueryCreation(uint8 *a)
{
    uint8 dates[FILE_DATE_BYTES];
    if (!load_dates(ARG(a, 0), dates))
        return ERROR_ACCESS_DENIED_CODE;
    if (ARG(a, 1))
        lx_execution_write(GProcessState.execution, ARG(a, 1), dates, 2);
    if (ARG(a, 2))
        lx_execution_write(GProcessState.execution, ARG(a, 2), dates + 2, 2);
    return NO_ERROR;
}

static uint32 wpQueryDateInfo(uint8 *a)
{
    uint8 dates[FILE_DATE_BYTES];
    return load_dates(ARG(a, 0), dates) &&
                   lx_execution_write(GProcessState.execution, ARG(a, 1) + FIND_BUFFER_DATES, dates,
                                      sizeof dates)
               ? NO_ERROR
               : ERROR_ACCESS_DENIED_CODE;
}

static uint32 wpSetDateInfo(uint8 *a)
{
    uint8 dates[FILE_DATE_BYTES];
    uint32 data = pmwpInstanceData(PMWP_WPFILESYSTEM, ARG(a, 0)) + FILE_DATES;
    return lx_execution_read(GProcessState.execution, ARG(a, 1) + FIND_BUFFER_DATES, dates,
                             sizeof dates) &&
                   lx_execution_write(GProcessState.execution, data, dates, sizeof dates)
               ? NO_ERROR
               : ERROR_ACCESS_DENIED_CODE;
}

static uint32 wpSetFileSizeInfo(uint8 *a)
{
    uint32 data = pmwpInstanceData(PMWP_WPFILESYSTEM, ARG(a, 0));
    pmwpStore32(data + FILE_SIZE, ARG(a, 1));
    pmwpStore32(data + FILE_EA_SIZE, ARG(a, 2));
    return NO_ERROR;
}

/* FILESTATUS4 reports the full-list size; PMWP halves it and treats 4 as empty. */
static uint32 wpQueryEASize(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char path[PATH_LIMIT];
    uint8 status[0x1c];
    uint32 size = 0;
    if (file_path(self, path, sizeof path) &&
        DosQueryPathInfo(path, 2, status, sizeof status) == NO_ERROR)
    {
        size = lx_guest_load_u32(status + 0x18);
        size = size > 4 ? size / 2 : 0;
    }
    pmwpStore32(pmwpInstanceData(PMWP_WPFILESYSTEM, self) + FILE_EA_SIZE, size);
    return size;
}

static uint32 wpRefreshFSInfo(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPFILESYSTEM, self);
    LxHostFileInfo info;
    if (!file_info(self, &info))
        return FALSE;
    uint8 cleared[FILE_DATE_BYTES] = {0};
    lx_execution_write(GProcessState.execution, data + FILE_DATES, cleared, sizeof cleared);
    pmwpStore32(data + FILE_SIZE, (uint32)info.size);
    return TRUE;
}

/* The drive object for a path, created the first time a drive is asked for. */
static uint32 disk_for_path(const char *path)
{
    if (!path[0] || path[1] != ':')
        return 0;
    char letter = (char)toupper((uint8)path[0]), value[8];
    size_t count = 0;
    ULONG *handles = pmwpStoreList(&count), found = 0;
    for (size_t i = 0; i < count && !found; i++)
    {
        PmwpRecord record = {0};
        if (!pmwpStoreLoad(handles[i], &record))
            continue;
        if (!strcmp(record.class_name, "WPDisk") &&
            pmwpSetupValue(record.setup, "DRIVENUM", value, sizeof value) &&
            strtol(value, NULL, 10) == letter - 'A' + 1)
            found = handles[i];
        pmwpStoreRelease(&record);
    }
    free(handles);
    if (!found)
    {
        char title[16], setup[16];
        snprintf(title, sizeof title, "Drive %c", letter);
        snprintf(setup, sizeof setup, "DRIVENUM=%d;", letter - 'A' + 1);
        found = pmwpStoreCreate("WPDisk", title, "<WP_DESKTOP>", setup);
    }
    return found ? pmwpAwaken(found) : 0;
}

static uint32 wpQueryDisk(uint8 *a)
{
    char path[PATH_LIMIT];
    return file_path(ARG(a, 0), path, sizeof path) ? disk_for_path(path) : 0;
}

/* Callers read the drive's object from this block, as they do from PMWP's drive table. */
static uint32 wpQueryDriveData(uint8 *a)
{
    static uint32 blocks[26];
    char path[PATH_LIMIT];
    uint32 disk = file_path(ARG(a, 0), path, sizeof path) ? disk_for_path(path) : 0;
    if (!disk)
        return 0;
    unsigned drive = (unsigned)(toupper((uint8)path[0]) - 'A');
    if (drive >= 26)
        return 0;
    if (!blocks[drive])
        blocks[drive] = pmwpCalloc(0x20);
    uint8 word[4];
    lx_guest_store_u32(word, disk);
    if (blocks[drive])
        lx_execution_write(GProcessState.execution, blocks[drive] + 0x13, word, sizeof word);
    return blocks[drive];
}

static uint32 wpSetNewRealName(uint8 *a)
{
    uint32 data = pmwpInstanceData(PMWP_WPFILESYSTEM, ARG(a, 0));
    char *name = lx_execution_copy_string(GProcessState.execution, ARG(a, 1), 256);
    pmwpFree(pmwpLoad32(data + FILE_PENDING_NAME));
    pmwpStore32(data + FILE_PENDING_NAME, name ? pmwpGuestString(name) : 0);
    BOOL result = name != NULL;
    free(name);
    return result;
}

static uint32 wpAdoptNewRealName(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPFILESYSTEM, self);
    uint32 name = pmwpLoad32(data + FILE_PENDING_NAME);
    if (!name)
        return FALSE;
    BOOL result = pmwpInvoke(PMWP_WPFILESYSTEM, PMWP_WPFileSystem_wpSetRealName, self, name, 0, 0);
    pmwpFree(name);
    pmwpStore32(data + FILE_PENDING_NAME, 0);
    return result;
}

/* Read-only pages describing the object. */
static uint32 add_text_page(uint8 *a, const char *name, const char *format)
{
    uint32 self = ARG(a, 0);
    char path[PATH_LIMIT] = "", text[PATH_LIMIT + 96];
    LxHostFileInfo info = {0};
    file_path(self, path, sizeof path);
    file_info(self, &info);
    snprintf(text, sizeof text, format, path, (unsigned long)info.size,
             info.readonly ? "yes" : "no");
    return pmwpAddInfoPage(ARG(a, 1), name, text);
}

#define TEXT_PAGE(function, name, format)                                                          \
    static uint32 function(uint8 *a)                                                               \
    {                                                                                              \
        return add_text_page(a, name, format);                                                     \
    }
TEXT_PAGE(wpAddFile1Page, "File", "File: %s\nSize: %lu bytes\nRead-only: %s")
TEXT_PAGE(wpAddFile2Page, "Attributes", "File: %s\nSize: %lu bytes\nRead-only: %s")
TEXT_PAGE(wpAddFile3Page, "Dates", "File: %s\nSize: %lu bytes\nRead-only: %s")
TEXT_PAGE(wpAddFileMenuPage, "Menu", "File: %s\nSize: %lu bytes\nRead-only: %s")
TEXT_PAGE(wpAddFileTypePage, "Type", "File: %s\nSize: %lu bytes\nRead-only: %s")

static uint32 wpAddObjectWindowPage(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char text[96];
    snprintf(text, sizeof text, "Button: %u\nMinimize: %u\nConcurrent view: %u",
             (unsigned)pmwpObjectField(self, PMWP_OBJ_BUTTON),
             (unsigned)pmwpObjectField(self, PMWP_OBJ_MIN_WINDOW),
             (unsigned)pmwpObjectField(self, PMWP_OBJ_CONCURRENT_VIEW));
    return pmwpAddInfoPage(ARG(a, 1), "Window", text);
}

/* A shadow's views, menus and drag behaviour are those of the object it links to. */
static uint32 shadow_target(uint32 self)
{
    PmwpRecord record = {0};
    char id[SHADOW_LIMIT];
    uint32 target = 0;
    if (handle_of(self) && pmwpStoreLoad(handle_of(self), &record))
    {
        if (pmwpSetupValue(record.setup, "SHADOWID", id, sizeof id))
        {
            ULONG handle = pmwpStoreFind(id);
            target = handle ? pmwpAwaken(handle) : pmwpObjectForPath(id);
        }
        pmwpStoreRelease(&record);
    }
    return target;
}

#define SHADOW_FORWARD(method)                                                                     \
    static uint32 shd_##method(uint8 *a)                                                           \
    {                                                                                              \
        uint32 target = shadow_target(ARG(a, 0));                                                  \
        return target ? pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_##method, target, ARG(a, 1),       \
                                   ARG(a, 2), ARG(a, 3))                                           \
                      : 0;                                                                         \
    }
SHADOW_FORWARD(wpOpen)
SHADOW_FORWARD(wpViewObject)
SHADOW_FORWARD(wpFilterPopupMenu)
SHADOW_FORWARD(wpModifyPopupMenu)
SHADOW_FORWARD(wpMenuItemSelected)
SHADOW_FORWARD(wpPrintObject)
SHADOW_FORWARD(wpDragOver)
SHADOW_FORWARD(wpFormatDragItem)
SHADOW_FORWARD(wpDraggedOverObject)
SHADOW_FORWARD(wpDroppedOnObject)
SHADOW_FORWARD(wpQueryDefaultHelp)

static uint32 shd_wpQueryStyle(uint8 *a)
{
    uint32 target = shadow_target(ARG(a, 0));
    return target ? pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryStyle, target, 0, 0, 0)
                  : pmwpObjectField(ARG(a, 0), PMWP_OBJ_STYLE);
}

static uint32 shd_wpQueryTitle(uint8 *a)
{
    uint32 target = shadow_target(ARG(a, 0));
    return target ? pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryTitle, target, 0, 0, 0)
                  : pmwpObjectField(ARG(a, 0), PMWP_OBJ_TITLE);
}

static uint32 shd_wpInsertPopupMenuItems(uint8 *a)
{
    uint32 target = shadow_target(ARG(a, 0));
    const uint32 words[] = {target, ARG(a, 1), ARG(a, 2), ARG(a, 3), ARG(a, 4), ARG(a, 5)};
    return target
               ? pmwpGuestCallMany(
                     pmwpClassToken(PMWP_WPOBJECT, PMWP_WPObject_wpInsertPopupMenuItems), words, 6)
               : FALSE;
}

/* Class lookup follows WinReplaceObjectClass: a replaced class resolves to its
   replacement before the SOM class manager searches. */
static uint32 replacement_id(uint32 id, uint32 *owned)
{
    *owned = 0;
    char *name = lx_execution_copy_string(GProcessState.execution, pmwpSomStringFromId(id), 256);
    char replacement[256], next[256];
    BOOL replaced = FALSE;
    if (name)
    {
        snprintf(replacement, sizeof replacement, "%s", name);
        for (unsigned depth = 0; depth < 16 && pmwpStoreReplacement(replacement, next, sizeof next);
             depth++)
        {
            memcpy(replacement, next, sizeof replacement);
            replaced = TRUE;
        }
    }
    free(name);
    if (!replaced)
        return id;
    uint32 text = pmwpGuestString(replacement);
    uint32 result = text ? pmwpSomIdFromString(text) : 0;
    pmwpFree(text);
    *owned = result;
    return result ? result : id;
}

static uint32 wpcm_somFindClass(uint8 *a)
{
    uint32 owned, id = replacement_id(ARG(a, 1), &owned);
    uint32 result = pmwpGuestCall(pmwpClassManagerParent(PMWP_SOMClassMgr_somFindClass), ARG(a, 0),
                                  id, ARG(a, 2), ARG(a, 3));
    pmwpFree(owned);
    return result;
}

static uint32 wpcm_somFindClsInFile(uint8 *a)
{
    uint32 owned, id = replacement_id(ARG(a, 1), &owned);
    const uint32 words[] = {ARG(a, 0), id, ARG(a, 2), ARG(a, 3), ARG(a, 4)};
    uint32 result =
        pmwpGuestCallMany(pmwpClassManagerParent(PMWP_SOMClassMgr_somFindClsInFile), words, 5);
    pmwpFree(owned);
    return result;
}

static uint32 wpSetTitleNoRenameFile(uint8 *a)
{
    return pmwpObjectSetTitle(ARG(a, 0), ARG(a, 1));
}

static uint32 wpQueryShadowedObject(uint8 *a)
{
    uint32 target = shadow_target(ARG(a, 0));
    if (target && ARG(a, 1))
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpLockObject, target, 0, 0, 0);
    return target;
}

/* File dates, sizes and refresh flags are saved here; the type lives in the store, and
   the parent saves the object state. */
static uint32 fs_wpSaveState(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPFILESYSTEM, self);
    uint8 state[FILE_STATE_BYTES];
    if (!lx_execution_read(GProcessState.execution, data + FILE_DATES, state, FILE_DATE_BYTES))
        return FALSE;
    lx_guest_store_u32(state + FILE_DATE_BYTES, pmwpLoad32(data + FILE_SIZE));
    lx_guest_store_u32(state + FILE_DATE_BYTES + 4, pmwpLoad32(data + FILE_EA_SIZE));
    lx_guest_store_u32(state + FILE_DATE_BYTES + 8, pmwpLoad32(data + FILE_REFRESH_FLAGS));
    return pmwpGuestCall(pmwpParentMethod(PMWP_WPFILESYSTEM,
                                          pmwpClassToken(PMWP_WPOBJECT, PMWP_WPObject_wpSaveState)),
                         self, 0, 0, 0) &&
           pmwpStoreSetValue(handle_of(self), "WPFileSystem", FILE_STATE_KEY, state, sizeof state);
}

static uint32 fs_wpRestoreState(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPFILESYSTEM, self);
    BOOL result =
        pmwpGuestCall(pmwpParentMethod(PMWP_WPFILESYSTEM,
                                       pmwpClassToken(PMWP_WPOBJECT, PMWP_WPObject_wpRestoreState)),
                      self, ARG(a, 1), 0, 0);
    uint8 state[FILE_STATE_BYTES];
    ULONG size = sizeof state;
    if (pmwpStoreGetValue(handle_of(self), "WPFileSystem", FILE_STATE_KEY, state, &size) &&
        size == sizeof state)
    {
        lx_execution_write(GProcessState.execution, data + FILE_DATES, state, FILE_DATE_BYTES);
        pmwpStore32(data + FILE_SIZE, lx_guest_load_u32(state + FILE_DATE_BYTES));
        pmwpStore32(data + FILE_EA_SIZE, lx_guest_load_u32(state + FILE_DATE_BYTES + 4));
        pmwpStore32(data + FILE_REFRESH_FLAGS, lx_guest_load_u32(state + FILE_DATE_BYTES + 8));
    }
    return result;
}

static uint32 fs_wpSetup(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char *setup =
        ARG(a, 1) ? lx_execution_copy_string(GProcessState.execution, ARG(a, 1), 32768) : NULL;
    char value[4097];
    if (setup && pmwpSetupValue(setup, "TITLENORENAME", value, sizeof value))
    {
        uint32 title = pmwpGuestString(value);
        if (title)
            pmwpObjectSetTitle(self, title);
        pmwpFree(title);
    }
    free(setup);
    return pmwpGuestCall(
        pmwpParentMethod(PMWP_WPFILESYSTEM, pmwpClassToken(PMWP_WPOBJECT, PMWP_WPObject_wpSetup)),
        self, ARG(a, 1), 0, 0);
}

/* A shadow must link to something that exists. */
static uint32 shd_wpSetup(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char *setup =
        ARG(a, 1) ? lx_execution_copy_string(GProcessState.execution, ARG(a, 1), 32768) : NULL;
    char id[SHADOW_LIMIT];
    BOOL valid = !setup || !pmwpSetupValue(setup, "SHADOWID", id, sizeof id) || pmwpStoreFind(id) ||
                 pmwpPathKind(id);
    free(setup);
    return valid &&
           pmwpGuestCall(pmwpParentMethod(PMWP_WPSHADOW,
                                          pmwpClassToken(PMWP_WPOBJECT, PMWP_WPObject_wpSetup)),
                         self, ARG(a, 1), 0, 0);
}

#define GENERATED_LIFETIME(name, id)                                                               \
    static uint32 init_##name(uint8 *a)                                                            \
    {                                                                                              \
        pmwpGeneratedInit(id, ARG(a, 0), ARG(a, 1));                                               \
        return 0;                                                                                  \
    }                                                                                              \
    static uint32 destruct_##name(uint8 *a)                                                        \
    {                                                                                              \
        pmwpGeneratedDestruct(id, ARG(a, 0), ARG(a, 1) & 0xff, ARG(a, 2));                         \
        return 0;                                                                                  \
    }
GENERATED_LIFETIME(abstract, PMWP_WPABSTRACT)
GENERATED_LIFETIME(file_system, PMWP_WPFILESYSTEM)
GENERATED_LIFETIME(folder, PMWP_WPFOLDER)
GENERATED_LIFETIME(data_file, PMWP_WPDATAFILE)
GENERATED_LIFETIME(transient, PMWP_WPTRANSIENT)
GENERATED_LIFETIME(desktop, PMWP_WPDESKTOP)
GENERATED_LIFETIME(disk, PMWP_WPDISK)
GENERATED_LIFETIME(program, PMWP_WPPROGRAM)
GENERATED_LIFETIME(shadow, PMWP_WPSHADOW)

static uint32 destruct_class_manager(uint8 *a)
{
    pmwpGeneratedDestruct(PMWP_WPCLASSMANAGER, ARG(a, 0), ARG(a, 1) & 0xff, ARG(a, 2));
    return 0;
}

/* Entries the original tables name withdrawn. Each forwards as PMWP.DLL does: path
   forms resolve the folder first and act with the object locked. */
static uint32 location_folder(uint32 location)
{
    char *path = lx_execution_copy_string(GProcessState.execution, location, PATH_LIMIT);
    uint32 folder = path ? pmwpFolderForLocation(path) : 0;
    free(path);
    return folder;
}

static uint32 withdrawn_confirmations(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryConfirmations, ARG(a, 0), 0, 0, 0);
}

static uint32 withdrawn_copy(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpCopyObject, ARG(a, 0),
                      location_folder(ARG(a, 1)), TRUE, 0);
}

static uint32 withdrawn_template(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpCreateFromTemplate, ARG(a, 0),
                      location_folder(ARG(a, 1)), TRUE, 0) != 0;
}

static uint32 withdrawn_shadow(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpCreateShadowObject, ARG(a, 0),
                      location_folder(ARG(a, 1)), TRUE, 0) != 0;
}

static uint32 withdrawn_move(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpMoveObject, ARG(a, 0),
                      location_folder(ARG(a, 1)), 0, 0);
}

static uint32 withdrawn_print(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpPrintObject, ARG(a, 0), ARG(a, 1), 0, 0);
}

/* Asks for the details size, then fills the caller's cursor. */
static uint32 withdrawn_details(uint8 *a)
{
    uint32 self = ARG(a, 0), size = pmwpCalloc(4);
    if (!size)
        return 0;
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryDetailsData, self, 0, size, 0);
    uint32 result =
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryDetailsData, self, ARG(a, 1), size, 0);
    pmwpFree(size);
    return result;
}

static uint32 withdrawn_new(uint8 *a)
{
    return pmwpNewObject(ARG(a, 0), ARG(a, 1), ARG(a, 2), ARG(a, 3), TRUE);
}

static uint32 withdrawn_folder(uint8 *a)
{
    return pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsQueryFolder, ARG(a, 0), ARG(a, 1), TRUE,
                      0);
}

static uint32 withdrawn_shadowed(uint8 *a)
{
    return pmwpInvoke(PMWP_WPSHADOW, PMWP_WPShadow_wpQueryShadowedObject, ARG(a, 0), TRUE, 0, 0);
}

/* The unqualified real name, in a block the object owns until the next call. */
static uint32 withdrawn_real_name(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPFILESYSTEM, self);
    char path[PATH_LIMIT];
    if (!file_path(self, path, sizeof path))
        return 0;
    const char *name = strrchr(path, '\\');
    name = name ? name + 1 : path;
    uint32 previous = pmwpLoad32(data + FILE_REAL_NAME);
    if (previous)
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpFreeMem, self, previous, 0, 0);
    uint32 size = (uint32)strlen(name) + 1;
    uint32 text = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpAllocMem, self, size, 0, 0);
    if (text)
        lx_execution_write(GProcessState.execution, text, name, size);
    pmwpStore32(data + FILE_REAL_NAME, text);
    return text;
}

/* A task list node is {next, task}; a task record counts its references at +4. */
static uint32 wpSetTaskRec(uint8 *a)
{
    uint32 self = ARG(a, 0), added = ARG(a, 1), removed = ARG(a, 2);
    uint32 link = pmwpInstanceData(PMWP_WPOBJECT, self) + PMWP_OBJ_TASKS;
    if (removed)
    {
        uint32 at = link;
        for (uint32 node = pmwpLoad32(at); node; at = node, node = pmwpLoad32(at))
            if (pmwpLoad32(node + 4) == removed)
            {
                pmwpStore32(at, pmwpLoad32(node));
                pmwpFree(node);
                pmwpStore32(removed + 4, pmwpLoad32(removed + 4) - 1);
                break;
            }
    }
    if (added)
    {
        uint32 node = pmwpMalloc(8);
        if (!node)
            return FALSE;
        pmwpStore32(node, pmwpLoad32(link));
        pmwpStore32(node + 4, added);
        pmwpStore32(link, node);
        pmwpStore32(added + 4, pmwpLoad32(added + 4) + 1);
    }
    return TRUE;
}

/* The cursor holds the previous task; it is cleared after the last one. */
static uint32 wpQueryTaskRec(uint8 *a)
{
    uint32 cursor = ARG(a, 1), current = pmwpLoad32(cursor);
    uint32 node = pmwpObjectField(ARG(a, 0), PMWP_OBJ_TASKS);
    if (current)
    {
        while (node && pmwpLoad32(node + 4) != current)
            node = pmwpLoad32(node);
        node = node ? pmwpLoad32(node) : 0;
    }
    return pmwpStore32(cursor, node ? pmwpLoad32(node + 4) : 0);
}

static uint32 wpRemoveFileAttrs(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint32 attributes = pmwpInvoke(PMWP_WPFILESYSTEM, PMWP_WPFileSystem_wpQueryAttr, self, 0, 0, 0);
    pmwpInvoke(PMWP_WPFILESYSTEM, PMWP_WPFileSystem_wpSetAttr, self, attributes & ~ARG(a, 1), 0, 0);
    return TRUE;
}

/* The keys are one comma-separated string; a key already present is not repeated. */
static uint32 wpAddWindowPosKey(uint8 *a)
{
    uint32 self = ARG(a, 0), keys = pmwpObjectField(self, PMWP_OBJ_WINDOW_KEYS);
    char *key = lx_execution_copy_string(GProcessState.execution, ARG(a, 1), 256);
    char *list = keys ? lx_execution_copy_string(GProcessState.execution, keys, WINDOW_KEYS_LIMIT)
                      : strdup("");
    BOOL result = FALSE;
    if (key && list)
    {
        char joined[WINDOW_KEYS_LIMIT];
        result = TRUE;
        if (!strstr(list, key) && strlen(list) + strlen(key) + 2 < sizeof joined)
        {
            snprintf(joined, sizeof joined, "%s%s%s", list, *list ? "," : "", key);
            pmwpFree(keys);
            pmwpSetObjectField(self, PMWP_OBJ_WINDOW_KEYS, pmwpGuestString(joined));
            pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSaveDeferred, self, 0, 0, 0);
        }
    }
    free(key);
    free(list);
    return result;
}

static uint32 wpDeleteWindowPosKeys(uint8 *a)
{
    uint32 self = ARG(a, 0), keys = pmwpObjectField(self, PMWP_OBJ_WINDOW_KEYS);
    if (!keys)
        return TRUE;
    char *list = lx_execution_copy_string(GProcessState.execution, keys, WINDOW_KEYS_LIMIT);
    if (!list)
        return FALSE;
    for (char *key = list, *next; key && *key; key = next)
    {
        next = strchr(key, ',');
        if (next)
            *next++ = 0;
        PrfWriteProfileData(HINI_USERPROFILE, "PM_Workplace:FolderPos", key, NULL, 0);
    }
    free(list);
    pmwpFree(keys);
    pmwpSetObjectField(self, PMWP_OBJ_WINDOW_KEYS, 0);
    return TRUE;
}

/* wpAddUserMenuItem has no provider, so there is never an item to insert. */
static uint32 wpAddUserItemsToPopupMenu(uint8 *a)
{
    (void)a;
    return TRUE;
}

/* Most methods without a provider report failure as zero: FALSE, a null pointer, a null
   window or page identifier. These return an OS/2 status, where zero means success. */
uint32 pmwpMethodFailure(const char *method)
{
    static const char *const status_methods[] = {"wpRequestFolderMutexSem",
                                                 "wpReleaseFolderMutexSem",
                                                 "wpRequestFindMutexSem",
                                                 "wpReleaseFindMutexSem",
                                                 "wpEjectDisk",
                                                 "wpLockDrive",
                                                 "wpQueryDriveLockStatus",
                                                 "wpSetFileSizeL"};
    for (size_t i = 0; i < sizeof status_methods / sizeof *status_methods; i++)
        if (!strcmp(status_methods[i], method))
            return PMWP_ERROR_NO_PROVIDER;
    return 0;
}

const PmwpBody pmwpExtendedBodies[] = {
    {"WPFileSystem", "wpSetTitleNoRenameFile", wpSetTitleNoRenameFile, 8},
    {"WPFileSystem", "wpSaveState", fs_wpSaveState, 4},
    {"WPFileSystem", "wpRestoreState", fs_wpRestoreState, 8},
    {"WPFileSystem", "wpSetup", fs_wpSetup, 8},
    {"WPShadow", "wpSetup", shd_wpSetup, 8},
    {"WPAbstract", "somDefaultInit", init_abstract, 8},
    {"WPAbstract", "somDestruct", destruct_abstract, 12},
    {"WPFileSystem", "somDefaultInit", init_file_system, 8},
    {"WPFileSystem", "somDestruct", destruct_file_system, 12},
    {"WPFolder", "somDefaultInit", init_folder, 8},
    {"WPFolder", "somDestruct", destruct_folder, 12},
    {"WPDataFile", "somDefaultInit", init_data_file, 8},
    {"WPDataFile", "somDestruct", destruct_data_file, 12},
    {"WPTransient", "somDefaultInit", init_transient, 8},
    {"WPTransient", "somDestruct", destruct_transient, 12},
    {"WPDesktop", "somDefaultInit", init_desktop, 8},
    {"WPDesktop", "somDestruct", destruct_desktop, 12},
    {"WPDisk", "somDefaultInit", init_disk, 8},
    {"WPDisk", "somDestruct", destruct_disk, 12},
    {"WPProgram", "somDefaultInit", init_program, 8},
    {"WPProgram", "somDestruct", destruct_program, 12},
    {"WPShadow", "somDefaultInit", init_shadow, 8},
    {"WPShadow", "somDestruct", destruct_shadow, 12},
    {"WPClassManager", "somDestruct", destruct_class_manager, 12},
    {"WPObject", "withdrawn2", withdrawn_confirmations, 4},
    {"WPObject", "withdrawn5", withdrawn_copy, 8},
    {"WPObject", "withdrawn3", withdrawn_template, 8},
    {"WPObject", "withdrawn4", withdrawn_shadow, 8},
    {"WPObject", "withdrawn6", withdrawn_move, 8},
    {"WPObject", "withdrawn11", withdrawn_print, 8},
    {"WPObject", "withdrawn7", withdrawn_details, 8},
    {"M_WPObject", "withdrawn8", withdrawn_new, 16},
    {"M_WPObject", "withdrawn9", withdrawn_folder, 8},
    {"WPFileSystem", "withdrawn_wpQueryRealName", withdrawn_real_name, 4},
    {"WPShadow", "withdrawn", withdrawn_shadowed, 4},
    {"WPObject", "wpSetTaskRec", wpSetTaskRec, 12},
    {"WPObject", "wpQueryTaskRec", wpQueryTaskRec, 8},
    {"WPFileSystem", "wpRemoveFileAttrs", wpRemoveFileAttrs, 8},
    {"WPObject", "wpAddWindowPosKey", wpAddWindowPosKey, 8},
    {"WPObject", "wpDeleteWindowPosKeys", wpDeleteWindowPosKeys, 4},
    {"WPFileSystem", "wpAddUserItemsToPopupMenu", wpAddUserItemsToPopupMenu, 16},
    {"WPShadow", "wpQueryShadowedObject", wpQueryShadowedObject, 8},
    {"WPObject", "wpSetError", wpSetError, 8},
    {"WPObject", "wpQueryError", wpQueryError, 4},
    {"WPObject", "wpSetIcon", wpSetIcon, 8},
    {"WPObject", "wpQueryIcon", wpQueryIcon, 4},
    {"WPObject", "wpSetIconData", wpSetIconData, 8},
    {"WPObject", "wpQueryIconData", wpQueryIconData, 8},
    {"WPObject", "wpQueryState", wpQueryState, 4},
    {"WPObject", "wpSetState", wpSetState, 8},
    {"WPObject", "wpModifyState", wpModifyState, 12},
    {"WPObject", "wpSetNextObj", wpSetNextObj, 8},
    {"WPObject", "wpQueryNextObj", wpQueryNextObj, 4},
    {"WPObject", "_get_pobjNext", get_next_pointer, 4},
    {"WPObject", "wpSetInsertState", wpSetInsertState, 8},
    {"WPObject", "wpQueryIconSaved", wpQueryIconSaved, 4},
    {"WPObject", "wpQueryContainer", wpQueryContainer, 4},
    {"WPObject", "wpSetContainer", wpSetContainer, 8},
    {"WPObject", "wpQueryContainerFlagPtr", wpQueryContainerFlagPtr, 4},
    {"WPObject", "wpQueryCoreRecord", wpQueryCoreRecord, 4},
    {"WPObject", "wpQueryScreenGroupID", wpQueryScreenGroupID, 8},
    {"WPObject", "wpIdentify", wpIdentify, 8},
    {"WPObject", "wpBeginSave", base_true, 4},
    {"WPObject", "wpEndSave", base_true, 4},
    {"WPObject", "wpBeginRestore", wpBeginRestore, 8},
    {"WPObject", "wpEndRestore", wpEndRestore, 4},
    {"WPObject", "wpAppendSaveData", wpAppendSaveData, 24},
    {"WPObject", "wpExtractRestoreData", wpExtractRestoreData, 24},
    {"WPObject", "wpMakeDormant", wpMakeDormant, 8},
    {"WPObject", "wpReplaceObject", wpReplaceObject, 12},
    {"WPObject", "wpConfirmObjectTitle", wpConfirmObjectTitle, 24},
    {"WPObject", "wpQueryConfirmations", confirmations_default, 4},
    {"WPObject", "wpWaitForClose", wpWaitForClose, 20},
    {"WPObject", "wpAppendObject", base_zero, 12},
    {"WPObject", "wpDoesObjectMatch", base_true, 8},
    {"WPObject", "wpEndConversation", base_zero, 12},
    {"WPObject", "wpQueryNameClashOptions", name_clash_options, 8},
    {"WPObject", "wpDestroyObject", base_true, 4},
    {"WPObject", "wpMakeObject", base_zero, 24},
    {"WPObject", "wpAddObjectWindowPage", wpAddObjectWindowPage, 8},
    {"M_WPObject", "wpclsQueryDetails", base_zero, 4},
    {"M_WPObject", "wpclsQueryExtendedCriteria", base_true, 16},
    {"M_WPObject", "wpclsQuerySearchInfo", base_zero, 16},
    {"M_WPObject", "wpclsQueryMaxAnimationIcons", base_zero, 4},
    {"M_WPObject", "wpclsMakeAwake", wpclsMakeAwake, 28},
    {"M_WPObject", "wpclsSaveDirtyObjects", wpclsSaveDirtyObjects, 4},
    {"M_WPObject", "wpclsLockDirtyList", lock_dirty, 4},
    {"M_WPObject", "wpclsUnlockDirtyList", unlock_dirty, 4},
    {"M_WPObject", "wpclsLockSleepyList", lock_sleepy, 4},
    {"M_WPObject", "wpclsUnlockSleepyList", unlock_sleepy, 4},
    {"M_WPObject", "wpclsLockRestartList", lock_restart, 4},
    {"M_WPObject", "wpclsUnlockRestartList", unlock_restart, 4},
    {"WPFileSystem", "wpQueryCreation", wpQueryCreation, 12},
    {"WPFileSystem", "wpQueryDateInfo", wpQueryDateInfo, 8},
    {"WPFileSystem", "wpSetDateInfo", wpSetDateInfo, 8},
    {"WPFileSystem", "wpSetFileSizeInfo", wpSetFileSizeInfo, 12},
    {"WPFileSystem", "wpQueryEASize", wpQueryEASize, 4},
    {"WPFileSystem", "wpRefreshFSInfo", wpRefreshFSInfo, 16},
    {"WPFileSystem", "wpQueryDisk", wpQueryDisk, 4},
    {"WPFileSystem", "wpQueryDriveData", wpQueryDriveData, 4},
    {"WPFileSystem", "wpSetNewRealName", wpSetNewRealName, 8},
    {"WPFileSystem", "wpAdoptNewRealName", wpAdoptNewRealName, 4},
    {"WPFileSystem", "wpConfirmRenameFileWithExt", keep_rename_default, 4},
    {"WPFileSystem", "wpConfirmKeepAssoc", keep_rename_default, 4},
    {"WPFileSystem", "wpAddFile1Page", wpAddFile1Page, 8},
    {"WPFileSystem", "wpAddFile2Page", wpAddFile2Page, 8},
    {"WPFileSystem", "wpAddFile3Page", wpAddFile3Page, 8},
    {"WPFileSystem", "wpAddFileMenuPage", wpAddFileMenuPage, 8},
    {"WPDataFile", "wpAddFileTypePage", wpAddFileTypePage, 8},
    {"WPClassManager", "somFindClass", wpcm_somFindClass, 16},
    {"WPClassManager", "somFindClsInFile", wpcm_somFindClsInFile, 20},
    {"WPShadow", "wpOpen", shd_wpOpen, 16},
    {"WPShadow", "wpViewObject", shd_wpViewObject, 16},
    {"WPShadow", "wpFilterPopupMenu", shd_wpFilterPopupMenu, 16},
    {"WPShadow", "wpModifyPopupMenu", shd_wpModifyPopupMenu, 16},
    {"WPShadow", "wpMenuItemSelected", shd_wpMenuItemSelected, 12},
    {"WPShadow", "wpPrintObject", shd_wpPrintObject, 12},
    {"WPShadow", "wpDragOver", shd_wpDragOver, 12},
    {"WPShadow", "wpFormatDragItem", shd_wpFormatDragItem, 8},
    {"WPShadow", "wpDraggedOverObject", shd_wpDraggedOverObject, 8},
    {"WPShadow", "wpDroppedOnObject", shd_wpDroppedOnObject, 8},
    {"WPShadow", "wpQueryDefaultHelp", shd_wpQueryDefaultHelp, 12},
    {"WPShadow", "wpQueryStyle", shd_wpQueryStyle, 4},
    {"WPShadow", "wpQueryTitle", shd_wpQueryTitle, 4},
    {"WPShadow", "wpInsertPopupMenuItems", shd_wpInsertPopupMenuItems, 24},
    {"WPObject", "wpQueryButtonAppearance", wpQueryButtonAppearance, 4},
    {"WPObject", "wpSetButtonAppearance", wpSetButtonAppearance, 8},
    {"WPObject", "wpQueryMinWindow", wpQueryMinWindow, 4},
    {"WPObject", "wpSetMinWindow", wpSetMinWindow, 8},
    {"WPObject", "wpSetDefaultIconPos", wpSetDefaultIconPos, 8},
    {"WPObject", "wpQueryDefaultIconPos", wpQueryDefaultIconPos, 8},
    {"WPObject", "wpIsObjectInitialized", wpIsObjectInitialized, 4},
    {"WPObject", "wpHide", wpHide, 4},
    {"WPObject", "wpRestore", wpRestore, 4},
    {"WPObject", "wpFindViewItem", wpFindViewItem, 12},
    {"WPObject", "wpCreateAnother", wpCreateAnother, 16},
    {"WPObject", "wpCreateFromTemplate", wpCreateFromTemplate, 12},
    {"WPObject", "wpConfirmDelete", wpConfirmDelete, 8},
    {"WPObject", "wpRequestObjectMutexSem", wpRequestObjectMutexSem, 8},
    {"WPObject", "wpReleaseObjectMutexSem", wpReleaseObjectMutexSem, 4},
    {"WPObject", "wpAssertObjectMutexSem", wpAssertObjectMutexSem, 4},
    {"WPObject", "wpDrop", wpDrop, 16},
    {"WPObject", "wpDraggedOverObject", wpDraggedOverObject, 8},
    {"M_WPObject", "wpclsQueryIconData", wpclsQueryIconData, 8},
    {"M_WPObject", "wpclsSetIcon", wpclsSetIcon, 8},
    {"M_WPObject", "wpclsSetIconData", wpclsSetIconData, 8},
    {"M_WPObject", "wpclsSetSettingsPageSize", wpclsSetSettingsPageSize, 8},
    {"M_WPObject", "wpclsQueryButtonAppearance", wpclsQueryButtonAppearance, 4},
    {"WPFileSystem", "wpQueryFileSize", wpQueryFileSize, 4},
    {"WPFileSystem", "wpQueryAttr", wpQueryAttr, 4},
    {"WPFileSystem", "wpSetAttr", wpSetAttr, 8},
    {"WPFileSystem", "wpQueryLastWrite", wpQueryLastWrite, 12},
    {"WPFileSystem", "wpQueryLastAccess", wpQueryLastAccess, 12},
    {"WPFileSystem", "wpVerifyUpdateAccess", wpVerifyUpdateAccess, 4},
    {"WPFileSystem", "wpSetRealName", wpSetRealName, 8},
    {"WPFileSystem", "wpSetTitleAndRenameFile", wpSetTitleAndRenameFile, 12},
    {"WPFileSystem", "wpQueryRefreshFlags", wpQueryRefreshFlags, 4},
    {"WPFileSystem", "wpSetRefreshFlags", wpSetRefreshFlags, 8},
    {"WPFileSystem", "wpIsDiskSwapped", wpIsDiskSwapped, 4},
    {"M_WPFileSystem", "wpclsQueryAwakeObject", wpclsQueryAwakeObject, 8},
    {"M_WPFileSystem", "wpclsFileSysExists", wpclsFileSysExists, 16},
    {"WPClassManager", "wpModuleForClass", wpModuleForClass, 8},
    {"WPClassManager", "wpReplacementIsInEffect", wpReplacementIsInEffect, 12},
    {"WPClassManager", "wpGetTrueClassName", wpGetTrueClassName, 8},
};

const size_t pmwpExtendedBodyCount = sizeof pmwpExtendedBodies / sizeof *pmwpExtendedBodies;
