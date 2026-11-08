/* Public domain. No rights reserved. Wholly AI-authored. */
/* M_WPObject class services and the file system, folder, disk, program and data file
   classes, plus creation of objects of classes registered from a guest module. */
#include "os2libs/pmwp/internal.h"
#include "os2libs/doscalls.h"
#include "os2libs/pmdrag.h"
#include "os2libs/pmgpi.h"
#include "os2libs/pmwin.h"
#include "guest/abi.h"
#include "host/fs.h"
#include "host/sync.h"
#include <ctype.h>
#include <stdio.h>
#include <strings.h>

#define ARG(a, n) lx_guest_load_u32((a) + 4 * (n))

enum
{
    PATH_LIMIT = 1024,
    TITLE_LIMIT = 4097,
    SETUP_LIMIT = 32768,
    PAGE_MINIMUM_WIDTH = 220,
    PAGE_MINIMUM_HEIGHT = 100,
    CLASS_DEFAULT_STYLE = 0x90,
    SYSTEM_POINTER_FILE = 19,
    SYSTEM_POINTER_FOLDER = 20,
    SYSTEM_POINTER_PROGRAM = 22,
    DETAIL_COLUMNS = 5,
    DETAIL_RECORD = 0x54,
    DETAIL_DATA_BYTES = 12,
    DETAIL_DATA_FLAGS = 0x8202,
    DETAIL_TITLE_FLAGS = 0x10e04,
    WPERR_BUFFER_OVERFLOW_CODE = 0x1713,
    WPERR_OBJECT_NOT_FOUND_CODE = 0x1714,
    FIND_CLASS_LIMIT = 64,
    EA_ASCII = 0xfffd,
    EA_MULTI_VALUE = 0xffdf,
    DOS_COPY_FAIL_IF_EXISTS = 0,
    OR_FROMCOPY_CODE = 0x10000008,
    FILE_SYSTEM_ATTRIBUTE = 4,
    DRAG_MOVE = 0x20,
    DRAG_LINK = 0x18,
    DM_ENDCONVERSATION_MESSAGE = 0x32b,
    DMFL_TARGETSUCCESSFUL_FLAG = 1,
    DMFL_TARGETFAIL_FLAG = 2,
    QC_FIRST_CODE = 0,
    QC_NEXT_CODE = 1,
    QC_LAST_CODE = 2,
    OK_DELETE_RESULT = 1,
    NO_DELETE_RESULT = 2,
    FOLDER_FLAGS = 0x00,
    FOLDER_FLAGS_KEY = 1,
    FOLDER_VIEW_ICON = 1,
    FOLDER_VIEW_TREE = 101,
    FOLDER_VIEW_DETAILS = 102,
    DISK_DRIVE_KEY = 1,
    FILE_TYPE_TEXT = 0x00,
    FILE_TYPE_KEY = 1,
    DISK_DRIVE = 0x00,
    PROGRAM_DETAIL_BYTES = 72,
    PRINT_LINES = 60,
    DEVICE_QUEUED = 2,
    CAPS_WIDTH_INDEX = 4,
    CAPS_HEIGHT_INDEX = 5,
    ESCAPE_START_DOCUMENT = 8150,
    ESCAPE_END_DOCUMENT = 8151,
    ESCAPE_NEW_FRAME = 16300,
    PRINT_DEST_TYPE = 4,
    PRINT_DEST_TOKEN = 8,
    PRINT_DEST_COUNT = 12,
    PRINT_DEST_DATA = 16,
    PRINT_FIELD_LIMIT = 9
};

static uint32 class_of(uint32 self)
{
    return pmwpInvokeObject(PMWP_SOMObject_somGetClass, self, 0, 0, 0);
}

static ULONG handle_of(uint32 self)
{
    return pmwpObjectField(self, PMWP_OBJ_HANDLE);
}

/* Case-insensitive * and ? matching for titles and association filters. */
static BOOL wildcard(const char *pattern, const char *text)
{
    for (; *pattern; pattern++, text++)
    {
        if (*pattern == '*')
        {
            while (*pattern == '*')
                pattern++;
            if (!*pattern)
                return TRUE;
            for (; *text; text++)
                if (wildcard(pattern, text))
                    return TRUE;
            return FALSE;
        }
        if (!*text ||
            (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*text)))
            return FALSE;
    }
    return !*text;
}

static const char *base_name(const char *path)
{
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    return base;
}

/* Setup strings use a caret to escape their delimiters. */
static void quote_setup(char *output, size_t capacity, const char *text)
{
    size_t used = 0;
    for (; *text && used + 2 < capacity; text++)
    {
        if (*text == ';' || *text == ',' || *text == '^')
            output[used++] = '^';
        output[used++] = *text;
    }
    output[used] = 0;
}

/* The first object whose PATH key names this file or directory. A path with no object
   gains a record, as a file system object does when the shell first sees it. */
uint32 pmwpObjectForPath(const char *path)
{
    size_t count = 0;
    ULONG *handles = pmwpStoreList(&count), found = 0;
    for (size_t i = 0; i < count && !found; i++)
    {
        PmwpRecord record = {0};
        char stored[PATH_LIMIT];
        if (pmwpStoreLoad(handles[i], &record))
        {
            if (pmwpSetupValue(record.setup, "PATH", stored, sizeof stored) &&
                !strcasecmp(stored, path))
                found = handles[i];
            pmwpStoreRelease(&record);
        }
    }
    free(handles);
    if (found)
        return pmwpAwaken(found);
    int kind = pmwpPathKind(path);
    if (!kind)
        return 0;
    char quoted[2 * PATH_LIMIT], setup[2 * PATH_LIMIT + 8];
    quote_setup(quoted, sizeof quoted, path);
    snprintf(setup, sizeof setup, "PATH=%s;", quoted);
    const char *title = *base_name(path) ? base_name(path) : path;
    ULONG handle =
        pmwpStoreCreate(kind == 2 ? "WPFolder" : "WPDataFile", title, "<WP_DESKTOP>", setup);
    return handle ? pmwpAwaken(handle) : 0;
}

uint32 pmwpFolderForLocation(const char *location)
{
    ULONG handle = pmwpStoreFind(location);
    return handle ? pmwpAwaken(handle) : pmwpObjectForPath(location);
}

/* SOM's own class loader looks up <class>NewClass in the module in the same way. */
static uint32 load_class(const char *class_name, const char *module_name)
{
    if (!pmwpClassNew(PMWP_WPOBJECT, 0, 0))
        return 0;
    HMODULE module = 0;
    char entry_name[300];
    PFN entry = NULL;
    uint32 address = 0;
    snprintf(entry_name, sizeof entry_name, "%sNewClass", class_name);
    if (DosLoadModule(NULL, 0, (PSZ)module_name, &module) != NO_ERROR)
        return 0;
    if (DosQueryProcAddr(module, 0, entry_name, &entry) != NO_ERROR || !entry ||
        !lx_execution_entry_address(GLoaderState.execution, (void *)entry, &address))
    {
        DosFreeModule(module);
        return 0;
    }
    return pmwpGuestCall(address, 0, 0, 0, 0);
}

BOOL pmwpViewStored(ULONG handle, ULONG view)
{
    uint32 object = pmwpAwaken(handle);
    return object && pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpViewObject, object, 0, view, 0);
}

uint32 pmwpClassForName(const char *name)
{
    for (int id = 0; id < PMWP_CLASS_COUNT; id++)
        if (!strcmp(pmwpClassRows[id].name, name))
            return pmwpClassNew((PmwpClassId)id, 0, 0);
    char module[260];
    return pmwpStoreClassModule(name, module, sizeof module) ? load_class(name, module) : 0;
}

ULONG pmwpCreateRegistered(const char *class_name, const char *module, const char *title,
                           const char *setup, const char *location, ULONG flags)
{
    uint32 class_object = load_class(class_name, module);
    if (!class_object || !pmwpInvokeClass(PMWP_SOMClass_somDescendedFrom, class_object,
                                          pmwpClassObject(PMWP_WPOBJECT), 0, 0))
        return 0;
    uint32 guest_setup = pmwpGuestString(setup), guest_title = pmwpGuestString(title);
    ULONG result = 0;
    char identifier[PATH_LIMIT];
    ULONG existing = pmwpSetupValue(setup, "OBJECTID", identifier, sizeof identifier)
                         ? pmwpStoreFind(identifier)
                         : 0;
    if (!guest_setup || !guest_title || existing == 1 || (existing && flags == 0))
        goto done;
    if (existing && flags == 2)
    {
        uint32 object = pmwpAwaken(existing);
        if (object && pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetup, object, guest_setup, 0, 0))
            result = existing;
        goto done;
    }
    if (existing)
    {
        uint32 object = pmwpAwaken(existing);
        if (!object || !pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpFree, object, 0, 0, 0))
            goto done;
    }
    uint32 folder = pmwpFolderForLocation(location);
    uint32 object =
        folder ? pmwpNewObject(class_object, guest_title, guest_setup, folder, TRUE) : 0;
    result = object ? handle_of(object) : 0;
done:
    pmwpFree(guest_setup);
    pmwpFree(guest_title);
    return result;
}

static uint32 cls_somUninit(uint8 *a)
{
    pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsUnInitData, ARG(a, 0), 0, 0, 0);
    return 0;
}

static uint32 cls_somClassReady(uint8 *a)
{
    uint32 self = ARG(a, 0);
    pmwpGuestCall(pmwpParentProc(PMWP_M_WPOBJECT, PMWP_SOMClass_somClassReady), self, 0, 0, 0);
    pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsInitData, self, 0, 0, 0);
    return 0;
}

/* Workplace objects are created only by wpclsNew; PMWP.DLL refuses these entries. */
static uint32 cls_refuse_creation(uint8 *a)
{
    (void)a;
    return 0;
}

static uint32 wpclsInitData(uint8 *a)
{
    uint32 self = ARG(a, 0);
    pmwpSetClassField(self, PMWP_CLS_PAGE_WIDTH, PAGE_MINIMUM_WIDTH);
    pmwpSetClassField(self, PMWP_CLS_PAGE_HEIGHT, PAGE_MINIMUM_HEIGHT);
    return 0;
}

static uint32 wpclsUnInitData(uint8 *a)
{
    pmwpSetClassField(ARG(a, 0), PMWP_CLS_USAGE, 0);
    return 0;
}

static uint32 wpclsIncUsage(uint8 *a)
{
    uint32 self = ARG(a, 0);
    pmwpSetClassField(self, PMWP_CLS_USAGE, pmwpClassField(self, PMWP_CLS_USAGE) + 1);
    return 0;
}

static uint32 wpclsDecUsage(uint8 *a)
{
    uint32 self = ARG(a, 0), usage = pmwpClassField(self, PMWP_CLS_USAGE);
    if (usage)
        pmwpSetClassField(self, PMWP_CLS_USAGE, usage - 1);
    return 0;
}

#define CLASS_TITLE(name, text)                                                                    \
    static uint32 title_##name(uint8 *a)                                                           \
    {                                                                                              \
        (void)a;                                                                                   \
        return pmwpConstantString(text);                                                           \
    }
CLASS_TITLE(object, "Object")
CLASS_TITLE(abstract, "Abstract")
CLASS_TITLE(file_system, "File System")
CLASS_TITLE(folder, "Folder")
CLASS_TITLE(data_file, "Data File")
CLASS_TITLE(transient, "Transient")
CLASS_TITLE(desktop, "Desktop")
CLASS_TITLE(disk, "Disk")
CLASS_TITLE(program, "Program")
CLASS_TITLE(shadow, "Shadow")
/* Class values from the PMWP.DLL overrides. Class styles compose with the parent's, so
   each entry is the composed result; a default help panel has an empty library. */
#define CLASS_VALUE(name, value)                                                                   \
    static uint32 name(uint8 *a)                                                                   \
    {                                                                                              \
        (void)a;                                                                                   \
        return value;                                                                              \
    }
CLASS_VALUE(style_folder, 0x80)
CLASS_VALUE(style_data_file, 0x00)
CLASS_VALUE(style_transient, 0x90)
CLASS_VALUE(style_desktop, 0xd8)
CLASS_VALUE(style_disk, 0xda)
CLASS_VALUE(style_program, 0x80)
CLASS_VALUE(style_shadow, 0x90)
CLASS_VALUE(view_folder, 0x67)
CLASS_VALUE(view_data_file, 4)
CLASS_VALUE(view_desktop, 1)
CLASS_VALUE(view_disk, 0x78)
CLASS_VALUE(view_program, 4)
CLASS_VALUE(folder_animation_icons, 1)

static uint32 report_help(uint8 *a, uint32 panel)
{
    static const char library[] = "";
    return pmwpStore32(ARG(a, 1), panel) &&
           lx_execution_write(GLoaderState.execution, ARG(a, 2), library, sizeof library);
}

#define CLASS_HELP(name, panel)                                                                    \
    static uint32 help_##name(uint8 *a)                                                            \
    {                                                                                              \
        return report_help(a, panel);                                                              \
    }
CLASS_HELP(folder, 0x4fd)
CLASS_HELP(data_file, 0xff2)
CLASS_HELP(desktop, 0xfa0)
CLASS_HELP(disk, 0x1f4f)
CLASS_HELP(program, 0xff3)

/* The system pointer for the nearest of the folder and program classes. */
static uint32 wpclsQueryIcon(uint8 *a)
{
    uint32 self = ARG(a, 0), own = pmwpClassField(self, PMWP_CLS_ICON);
    if (own)
        return own;
    LONG pointer = SYSTEM_POINTER_FILE;
    if (pmwpInvokeClass(PMWP_SOMClass_somDescendedFrom, self, pmwpClassObject(PMWP_WPFOLDER), 0, 0))
        pointer = SYSTEM_POINTER_FOLDER;
    else if (pmwpInvokeClass(PMWP_SOMClass_somDescendedFrom, self, pmwpClassObject(PMWP_WPPROGRAM),
                             0, 0))
        pointer = SYSTEM_POINTER_PROGRAM;
    return WinQuerySysPointer(HWND_DESKTOP, pointer, FALSE);
}

static uint32 wpclsQueryStyle(uint8 *a)
{
    (void)a;
    return CLASS_DEFAULT_STYLE;
}

static uint32 wpclsQueryDefaultView(uint8 *a)
{
    (void)a;
    return UINT32_MAX;
}

static uint32 wpclsCreateDefaultTemplates(uint8 *a)
{
    (void)a;
    return FALSE;
}

static uint32 wpclsQueryDefaultHelp(uint8 *a)
{
    static const char library[] = "WPHELP.HLP";
    return pmwpStore32(ARG(a, 1), 0) &&
           lx_execution_write(GLoaderState.execution, ARG(a, 2), library, sizeof library);
}

/* Five columns, of which the last three read the 12 bytes from wpQueryDetailsData. The
   records are one guest array shared by every class, as in PMWP.DLL. */
static uint32 details_columns(void)
{
    static uint32 columns;
    static const char *const titles[DETAIL_COLUMNS] = {"Icon", "Title", "Object Title",
                                                       "Object Style", "Object Class"};
    uint32 current = __atomic_load_n(&columns, __ATOMIC_ACQUIRE);
    if (current)
        return current;
    uint32 block = pmwpCalloc(DETAIL_COLUMNS * DETAIL_RECORD);
    if (!block)
        return 0;
    for (uint32 i = 0; i < DETAIL_COLUMNS; i++)
    {
        uint32 record = block + i * DETAIL_RECORD;
        pmwpStore32(record, DETAIL_RECORD);
        pmwpStore32(record + 4, DETAIL_DATA_FLAGS);
        pmwpStore32(record + 8, DETAIL_TITLE_FLAGS);
        pmwpStore32(record + 0x0c, pmwpConstantString(titles[i]));
        pmwpStore32(record + 0x18, i + 1 < DETAIL_COLUMNS ? record + DETAIL_RECORD : 0);
        if (i >= 2)
        {
            pmwpStore32(record + 0x20, (i - 2) * 4);
            pmwpStore32(record + 0x24, 4);
        }
    }
    uint32 expected = 0;
    if (!__atomic_compare_exchange_n(&columns, &expected, block, 0, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE))
    {
        pmwpFree(block);
        return expected;
    }
    return block;
}

static uint32 wpclsQueryDetailsInfo(uint8 *a)
{
    uint32 head = details_columns();
    if (!head)
        return 0;
    if (ARG(a, 2))
        pmwpStore32(ARG(a, 2), DETAIL_DATA_BYTES);
    if (ARG(a, 1))
        pmwpStore32(ARG(a, 1), head);
    return DETAIL_COLUMNS;
}

static uint32 wpclsSetError(uint8 *a)
{
    pmwpSetClassField(ARG(a, 0), PMWP_CLS_ERROR, ARG(a, 1));
    return TRUE;
}

static uint32 wpclsQueryError(uint8 *a)
{
    return pmwpClassField(ARG(a, 0), PMWP_CLS_ERROR);
}

static uint32 wpclsQuerySettingsPageSize(uint8 *a)
{
    uint32 self = ARG(a, 0), size = ARG(a, 1);
    uint32 width = pmwpClassField(self, PMWP_CLS_PAGE_WIDTH);
    uint32 height = pmwpClassField(self, PMWP_CLS_PAGE_HEIGHT);
    return pmwpStore32(size, width < PAGE_MINIMUM_WIDTH ? PAGE_MINIMUM_WIDTH : width) &&
           pmwpStore32(size + 4, height < PAGE_MINIMUM_HEIGHT ? PAGE_MINIMUM_HEIGHT : height);
}

/* Subclasses raise the minimum page height; the width minimum stays the base one. */
static uint32 page_size_with_height(uint8 *a, uint32 minimum)
{
    if (!wpclsQuerySettingsPageSize(a))
        return FALSE;
    uint32 height = ARG(a, 1) + 4;
    return pmwpLoad32(height) >= minimum || pmwpStore32(height, minimum);
}

static uint32 page_size_120(uint8 *a)
{
    return page_size_with_height(a, 120);
}

static uint32 page_size_125(uint8 *a)
{
    return page_size_with_height(a, 125);
}

static uint32 wpclsNew(uint8 *a)
{
    return pmwpNewObject(ARG(a, 0), ARG(a, 1), ARG(a, 2), ARG(a, 3), ARG(a, 4));
}

static uint32 wpclsQueryFolder(uint8 *a)
{
    char *location = lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), PATH_LIMIT);
    uint32 folder = location ? pmwpFolderForLocation(location) : 0;
    free(location);
    if (folder && ARG(a, 2))
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpLockObject, folder, 0, 0, 0);
    return folder;
}

static uint32 wpclsObjectFromHandle(uint8 *a)
{
    return pmwpAwaken(ARG(a, 1));
}

/* A find handle holds a snapshot of matching store handles. Class filtering happens on
   delivery, because it needs the awake object. */
typedef struct FindState
{
    struct FindState *next;
    ULONG id;
    ULONG *handles;
    size_t count, at;
    uint32 classes[FIND_CLASS_LIMIT];
    size_t class_count;
} FindState;

static LxHostOnce find_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *find_lock;
static FindState *finds;
static ULONG next_find = 1;

static void initialize_find(void)
{
    find_lock = lx_host_monitor_create();
    if (!find_lock)
        abort();
}

static FindState *take_find(ULONG id)
{
    lx_host_once(&find_once, initialize_find);
    lx_host_monitor_lock(find_lock);
    FindState **link = &finds;
    while (*link && (*link)->id != id)
        link = &(*link)->next;
    FindState *state = *link;
    if (state)
        *link = state->next;
    lx_host_monitor_unlock(find_lock);
    return state;
}

static void put_find(FindState *state)
{
    lx_host_once(&find_once, initialize_find);
    lx_host_monitor_lock(find_lock);
    if (!state->id)
        state->id = next_find++;
    state->next = finds;
    finds = state;
    lx_host_monitor_unlock(find_lock);
}

static BOOL within_folder(const PmwpRecord *record, const char *folder, BOOL nested)
{
    if (!strcasecmp(record->location, folder))
        return TRUE;
    char location[PATH_LIMIT];
    snprintf(location, sizeof location, "%s", record->location);
    for (unsigned depth = 0; nested && depth < 64; depth++)
    {
        ULONG parent = pmwpStoreFind(location);
        PmwpRecord up = {0};
        if (parent <= 1 || !pmwpStoreLoad(parent, &up))
            return FALSE;
        snprintf(location, sizeof location, "%s", up.location);
        pmwpStoreRelease(&up);
        if (!strcasecmp(location, folder))
            return TRUE;
    }
    return FALSE;
}

static BOOL class_matches(const FindState *state, uint32 object)
{
    if (!state->class_count)
        return TRUE;
    for (size_t i = 0; i < state->class_count; i++)
        if (pmwpInvokeObject(PMWP_SOMObject_somIsA, object, state->classes[i], 0, 0))
            return TRUE;
    return FALSE;
}

/* TRUE reports the search is exhausted. A full buffer returns FALSE with
   WPERR_BUFFER_OVERFLOW so the caller continues with wpclsFindObjectNext. */
static BOOL deliver(uint32 self, FindState *state, uint32 buffer, uint32 count_address)
{
    uint32 capacity = pmwpLoad32(count_address), delivered = 0;
    while (state->at < state->count)
    {
        uint32 object = pmwpAwaken(state->handles[state->at]);
        if (object && class_matches(state, object))
        {
            if (delivered == capacity)
            {
                pmwpStore32(count_address, delivered);
                pmwpSetClassField(self, PMWP_CLS_ERROR, WPERR_BUFFER_OVERFLOW_CODE);
                return FALSE;
            }
            pmwpStore32(buffer + delivered++ * 4, object);
        }
        state->at++;
    }
    pmwpStore32(count_address, delivered);
    if (!delivered)
    {
        pmwpSetClassField(self, PMWP_CLS_ERROR, WPERR_OBJECT_NOT_FOUND_CODE);
        return FALSE;
    }
    return TRUE;
}

static uint32 wpclsFindObjectFirst(uint8 *a)
{
    uint32 self = ARG(a, 0), classes = ARG(a, 1), find_address = ARG(a, 2), folder = ARG(a, 4);
    FindState *state = calloc(1, sizeof *state);
    if (!state)
        return FALSE;
    for (uint32 at = classes; at && state->class_count < FIND_CLASS_LIMIT; at += 4)
    {
        uint32 entry = pmwpLoad32(at);
        if (!entry)
            break;
        state->classes[state->class_count++] = entry;
    }
    char *title =
        ARG(a, 3) ? lx_execution_copy_string(GLoaderState.execution, ARG(a, 3), TITLE_LIMIT) : NULL;
    char identifier[PATH_LIMIT] = "";
    ULONG parent = folder ? handle_of(folder) : 0;
    BOOL scoped = parent && pmwpStoreIdentifier(parent, identifier, sizeof identifier);
    size_t total = 0;
    ULONG *handles = pmwpStoreList(&total);
    state->handles = calloc(total ? total : 1, sizeof *state->handles);
    for (size_t i = 0; state->handles && i < total; i++)
    {
        PmwpRecord record = {0};
        if (!pmwpStoreLoad(handles[i], &record))
            continue;
        if ((!title || wildcard(title, record.title)) &&
            (!scoped || within_folder(&record, identifier, ARG(a, 5))))
            state->handles[state->count++] = handles[i];
        pmwpStoreRelease(&record);
    }
    free(handles);
    free(title);
    BOOL result = deliver(self, state, ARG(a, 7), ARG(a, 8));
    put_find(state);
    pmwpStore32(find_address, state->id);
    return result;
}

static uint32 wpclsFindObjectNext(uint8 *a)
{
    FindState *state = take_find(ARG(a, 1));
    if (!state)
        return FALSE;
    BOOL result = deliver(ARG(a, 0), state, ARG(a, 2), ARG(a, 3));
    put_find(state);
    return result;
}

static uint32 wpclsFindObjectEnd(uint8 *a)
{
    FindState *state = take_find(ARG(a, 1));
    if (!state)
        return FALSE;
    free(state->handles);
    free(state);
    return TRUE;
}

static uint32 file_empty_string(uint8 *a)
{
    (void)a;
    return pmwpConstantString("");
}

static uint32 wpclsQueryObjectFromPath(uint8 *a)
{
    char *path = lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), PATH_LIMIT);
    uint32 object = path ? pmwpObjectForPath(path) : 0;
    free(path);
    return object;
}

static BOOL object_file_name(uint32 self, char *path, size_t capacity, BOOL qualified)
{
    ULONG handle = handle_of(self);
    if (!handle || !pmwpStorePath(handle, path, capacity))
        return FALSE;
    if (!qualified)
        memmove(path, base_name(path), strlen(base_name(path)) + 1);
    return TRUE;
}

/* A null buffer returns the name length; otherwise the buffer size bounds the copy. */
static uint32 wpQueryRealName(uint8 *a)
{
    uint32 output = ARG(a, 1), size_address = ARG(a, 2);
    char path[PATH_LIMIT];
    if (!size_address || !object_file_name(ARG(a, 0), path, sizeof path, ARG(a, 3)))
        return FALSE;
    uint32 length = (uint32)strlen(path);
    if (!output)
        return pmwpStore32(size_address, length);
    uint32 capacity = pmwpLoad32(size_address);
    if (!capacity)
        return FALSE;
    uint32 copied = length + 1 < capacity ? length + 1 : capacity;
    path[copied - 1] = 0;
    return lx_execution_write(GLoaderState.execution, output, path, copied) &&
           pmwpStore32(size_address, copied - 1);
}

static uint32 wpQueryFilename(uint8 *a)
{
    uint32 output = ARG(a, 1);
    char path[PATH_LIMIT];
    return output && object_file_name(ARG(a, 0), path, sizeof path, ARG(a, 2)) &&
                   lx_execution_write(GLoaderState.execution, output, path, strlen(path) + 1)
               ? output
               : 0;
}

static uint32 wpRefresh(uint8 *a)
{
    char path[PATH_LIMIT];
    return object_file_name(ARG(a, 0), path, sizeof path, TRUE) && pmwpPathKind(path) != 0;
}

/* .TYPE holds a multi-value EA of ASCII strings, one per type. Types arrive separated by
   newlines. The record below is a FEALIST with that single attribute. */
static BOOL write_type_attribute(const char *path, const char *types)
{
    static const char name[] = ".TYPE";
    uint8 list[TITLE_LIMIT + 512];
    size_t value = 4 + 4 + sizeof name, at = value + 6, count = 0;
    for (const char *p = types; *p;)
    {
        const char *end = strchr(p, '\n');
        size_t length = end ? (size_t)(end - p) : strlen(p);
        if (at + 4 + length > sizeof list)
            return FALSE;
        lx_guest_store_u16(list + at, EA_ASCII);
        lx_guest_store_u16(list + at + 2, (uint16)length);
        memcpy(list + at + 4, p, length);
        at += 4 + length;
        count++;
        p += length + (end ? 1 : 0);
    }
    lx_guest_store_u16(list + value, EA_MULTI_VALUE);
    lx_guest_store_u16(list + value + 2, 0);
    lx_guest_store_u16(list + value + 4, (uint16)count);
    lx_guest_store_u32(list, (uint32)at);
    list[4] = 0;
    list[5] = sizeof name - 1;
    lx_guest_store_u16(list + 6, (uint16)(at - value));
    memcpy(list + 8, name, sizeof name);
    char *host = pmwpHostPath(path);
    LxHostFsError error;
    LxHostFile *file =
        host ? lx_host_file_open(host, LX_HOST_FILE_READ | LX_HOST_FILE_WRITE, 0, &error) : NULL;
    BOOL result = file && lx_host_file_metadata_set(file, list, at) == LX_HOST_FS_OK;
    if (file)
        lx_host_file_close(file);
    free(host);
    return result;
}

static uint32 wpSetType(uint8 *a)
{
    uint32 self = ARG(a, 0);
    ULONG handle = handle_of(self);
    char path[PATH_LIMIT];
    char *types = lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), TITLE_LIMIT);
    BOOL result =
        handle && types && object_file_name(self, path, sizeof path, TRUE) &&
        write_type_attribute(path, types) &&
        pmwpStoreSetValue(handle, "WPFileSystem", FILE_TYPE_KEY, types, (ULONG)strlen(types) + 1);
    free(types);
    return result;
}

BOOL pmwpObjectFilePath(uint32 self, char *path, size_t capacity)
{
    return object_file_name(self, path, capacity, TRUE);
}

/* File system objects carry their file: both operations act on the file first, then on
   the record, whose PATH is updated. The last PATH in a setup string takes effect. */
static BOOL target_file(uint32 folder, const char *path, char *target, size_t capacity)
{
    char directory[PATH_LIMIT];
    if (!folder || !object_file_name(folder, directory, sizeof directory, TRUE))
        return FALSE;
    size_t length = strlen(directory);
    snprintf(target, capacity, "%s%s%s", directory,
             length && directory[length - 1] == '\\' ? "" : "\\", base_name(path));
    return TRUE;
}

static void record_path(ULONG handle, const char *path)
{
    char quoted[2 * PATH_LIMIT], setup[2 * PATH_LIMIT + 8];
    quote_setup(quoted, sizeof quoted, path);
    snprintf(setup, sizeof setup, "PATH=%s;", quoted);
    pmwpStoreAppendSetup(handle, setup);
}

static uint32 fs_wpCopyObject(uint8 *a)
{
    uint32 self = ARG(a, 0), folder = ARG(a, 1);
    ULONG handle = handle_of(self), destination = folder ? handle_of(folder) : 0;
    char source[PATH_LIMIT], target[PATH_LIMIT + 260];
    if (!handle || !destination || pmwpObjectField(self, PMWP_OBJ_STYLE) & PMWP_OBJSTYLE_NOCOPY ||
        !object_file_name(self, source, sizeof source, TRUE) ||
        !target_file(folder, source, target, sizeof target) ||
        DosCopy(source, target, DOS_COPY_FAIL_IF_EXISTS) != NO_ERROR)
        return 0;
    ULONG copy = WinCopyObject(handle, destination, 0);
    if (!copy)
        return 0;
    record_path(copy, target);
    uint32 object = pmwpAwaken(copy);
    if (!object)
        return 0;
    if (ARG(a, 2))
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpLockObject, object, 0, 0, 0);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpObjectReady, object, OR_FROMCOPY_CODE, self, 0);
    return object;
}

static uint32 fs_wpMoveObject(uint8 *a)
{
    uint32 self = ARG(a, 0), folder = ARG(a, 1);
    ULONG handle = handle_of(self), destination = folder ? handle_of(folder) : 0;
    char source[PATH_LIMIT], target[PATH_LIMIT + 260];
    if (!handle || !destination || pmwpObjectField(self, PMWP_OBJ_STYLE) & PMWP_OBJSTYLE_NOMOVE ||
        !object_file_name(self, source, sizeof source, TRUE) ||
        !target_file(folder, source, target, sizeof target))
        return FALSE;
    char *from = pmwpHostPath(source), *to = pmwpHostPath(target);
    BOOL moved = from && to && lx_host_path_move(from, to) == LX_HOST_FS_OK;
    free(from);
    free(to);
    if (!moved || !WinMoveObject(handle, destination, 0))
        return FALSE;
    record_path(handle, target);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetFolder, self, folder, 0, 0);
    return TRUE;
}

/* A title change renames the file. Before the object has a file, it is only a title. */
static uint32 fs_wpSetTitle(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char path[PATH_LIMIT];
    if (!object_file_name(self, path, sizeof path, TRUE) || !pmwpPathKind(path))
        return pmwpObjectSetTitle(self, ARG(a, 1));
    uint32 confirmations =
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryConfirmations, self, 0, 0, 0);
    return pmwpInvoke(PMWP_WPFILESYSTEM, PMWP_WPFileSystem_wpSetTitleAndRenameFile, self, ARG(a, 1),
                      confirmations, 0);
}

static uint32 fs_wpQueryStyle(uint8 *a)
{
    uint32 self = ARG(a, 0), style = pmwpObjectField(self, PMWP_OBJ_STYLE);
    if (pmwpInvoke(PMWP_WPFILESYSTEM, PMWP_WPFileSystem_wpQueryAttr, self, 0, 0, 0) &
        FILE_SYSTEM_ATTRIBUTE)
        style |= PMWP_OBJSTYLE_NOMOVE | PMWP_OBJSTYLE_NODELETE | PMWP_OBJSTYLE_NORENAME;
    return style;
}

static uint32 fs_wpAddSettingsPages(uint8 *a)
{
    uint32 self = ARG(a, 0), book = ARG(a, 1);
    pmwpInvoke(PMWP_WPFILESYSTEM, PMWP_WPFileSystem_wpAddFile1Page, self, book, 0, 0);
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpAddObjectGeneralPage, self, book, 0, 0) != 0;
}

static uint32 wpQueryType(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPFILESYSTEM, self);
    char types[TITLE_LIMIT];
    ULONG size = sizeof types;
    if (!pmwpStoreGetValue(handle_of(self), "WPFileSystem", FILE_TYPE_KEY, types, &size))
        return pmwpConstantString("");
    uint32 previous = pmwpLoad32(data + FILE_TYPE_TEXT);
    if (previous)
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpFreeMem, self, previous, 0, 0);
    uint32 text = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpAllocMem, self, size, 0, 0);
    if (text)
        lx_execution_write(GLoaderState.execution, text, types, size);
    pmwpStore32(data + FILE_TYPE_TEXT, text);
    return text;
}

static uint32 fs_wpFormatDragItem(uint8 *a)
{
    uint32 self = ARG(a, 0), item = ARG(a, 1);
    char path[PATH_LIMIT];
    if (item && object_file_name(self, path, sizeof path, TRUE))
    {
        const char *name = base_name(path);
        if (!pmwpLoad32(item + 0x14))
            pmwpStore32(item + 0x14, DrgAddStrHandle((PSZ)name));
        if (!pmwpLoad32(item + 0x18))
            pmwpStore32(item + 0x18, DrgAddStrHandle((PSZ)name));
        if (!pmwpLoad32(item + 0x0c))
            pmwpStore32(item + 0x0c, DrgAddStrHandle("<DRM_OS2FILE,DRF_UNKNOWN>"));
        path[name - path] = 0;
        if (!pmwpLoad32(item + 0x10))
            pmwpStore32(item + 0x10, DrgAddStrHandle(path));
    }
    return pmwpFormatDragItem(self, item);
}

/* A file dropped on a program starts that program with the file as its parameter. */
static uint32 fs_wpDroppedOnObject(uint8 *a)
{
    uint32 target = ARG(a, 1);
    char path[PATH_LIMIT];
    uint32 program = pmwpClassObject(PMWP_WPPROGRAM);
    if (!target || !program || !pmwpInvokeObject(PMWP_SOMObject_somIsA, target, program, 0, 0) ||
        !object_file_name(ARG(a, 0), path, sizeof path, TRUE))
        return FALSE;
    return pmwpStoreStartProgram(handle_of(target), path);
}

/* The first program whose ASSOCFILTER matches the file name, or zero. */
static ULONG associated_program(const char *path)
{
    size_t count = 0;
    ULONG *handles = pmwpStoreList(&count), found = 0;
    for (size_t i = 0; i < count && !found; i++)
    {
        PmwpRecord record = {0};
        char filters[PATH_LIMIT];
        if (!pmwpStoreLoad(handles[i], &record))
            continue;
        if (!strcmp(record.class_name, "WPProgram") &&
            pmwpSetupValue(record.setup, "ASSOCFILTER", filters, sizeof filters))
        {
            char *rest = filters;
            for (char *filter = rest; filter && !found; filter = rest)
            {
                rest = strchr(filter, ',');
                if (rest)
                    *rest++ = 0;
                if (wildcard(filter, base_name(path)))
                    found = handles[i];
            }
        }
        pmwpStoreRelease(&record);
    }
    free(handles);
    return found;
}

static uint32 df_wpOpen(uint8 *a)
{
    uint32 self = ARG(a, 0), view = ARG(a, 2);
    if (view == PMWP_OPEN_SETTINGS)
        return pmwpOpenSettings(self);
    char path[PATH_LIMIT];
    if (!object_file_name(self, path, sizeof path, TRUE))
        return 0;
    ULONG program = associated_program(path);
    return program && pmwpStoreStartProgram(program, path);
}

static uint32 wpQueryAssociatedProgram(uint8 *a)
{
    char path[PATH_LIMIT];
    if (!object_file_name(ARG(a, 0), path, sizeof path, TRUE))
        return 0;
    ULONG program = associated_program(path);
    return program ? pmwpAwaken(program) : 0;
}

static uint32 wpQueryAssociatedFileIcon(uint8 *a)
{
    char path[PATH_LIMIT];
    ULONG handle =
        object_file_name(ARG(a, 0), path, sizeof path, TRUE) ? associated_program(path) : 0;
    uint32 program = handle ? pmwpAwaken(handle) : 0;
    return program ? pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpQueryIcon, program, 0, 0, 0) : 0;
}

static uint32 wpSetAssociatedFileIcon(uint8 *a)
{
    uint32 icon = wpQueryAssociatedFileIcon(a);
    if (icon)
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetIcon, ARG(a, 0), icon, 0, 0);
    return 0;
}

static BOOL print_lines(HDC device, HPS space, const char *text, size_t size, LONG height)
{
    LONG step = height / (PRINT_LINES + 2), line = 0;
    size_t at = 0;
    while (at < size)
    {
        size_t end = at;
        while (end < size && text[end] != '\n')
            end++;
        size_t length = end;
        if (length > at && text[length - 1] == '\r')
            length--;
        POINTL point = {step, height - step * (line + 2)};
        if (length > at)
            GpiCharStringAt(space, &point, (LONG)(length - at), (PCH)text + at);
        at = end + 1;
        if (++line == PRINT_LINES && at < size)
        {
            if (DevEscape(device, ESCAPE_NEW_FRAME, 0, NULL, NULL, NULL) < 0)
                return FALSE;
            line = 0;
        }
    }
    return TRUE;
}

/* Prints the file as plain text on the destination's device. */
static uint32 df_wpPrintObject(uint8 *a)
{
    uint32 self = ARG(a, 0), destination = ARG(a, 1);
    char path[PATH_LIMIT];
    if (!destination || !object_file_name(self, path, sizeof path, TRUE))
        return FALSE;
    LONG count = (LONG)pmwpLoad32(destination + PRINT_DEST_COUNT);
    if (count < 0 || count > PRINT_FIELD_LIMIT)
        return FALSE;
    char *fields[PRINT_FIELD_LIMIT] = {0};
    uint32 data = pmwpLoad32(destination + PRINT_DEST_DATA);
    for (LONG i = 0; i < count; i++)
    {
        uint32 field = pmwpLoad32(data + (uint32)i * 4);
        fields[i] = field ? lx_execution_copy_string(GLoaderState.execution, field, 256) : NULL;
    }
    uint32 token_address = pmwpLoad32(destination + PRINT_DEST_TOKEN);
    char *token = token_address
                      ? lx_execution_copy_string(GLoaderState.execution, token_address, 64)
                      : strdup("*");
    char *host = pmwpHostPath(path);
    FILE *file = host ? fopen(host, "rb") : NULL;
    char *text = NULL;
    long size = -1;
    if (file && !fseek(file, 0, SEEK_END) && (size = ftell(file)) >= 0 &&
        !fseek(file, 0, SEEK_SET) && (text = malloc((size_t)size + 1)))
        size = (long)fread(text, 1, (size_t)size, file);
    if (file)
        fclose(file);
    free(host);
    BOOL result = FALSE;
    HAB hab = WinQueryAnchorBlock(HWND_DESKTOP);
    HDC device = text && token ? DevOpenDC(hab, (LONG)pmwpLoad32(destination + PRINT_DEST_TYPE),
                                           token, count, fields, 0)
                               : 0;
    if (device)
    {
        LONG caps[2] = {0};
        SIZEL page = {0, 0};
        HPS space = DevQueryCaps(device, CAPS_WIDTH_INDEX, 2, caps)
                        ? GpiCreatePS(hab, device, &page, PU_PELS | GPIA_ASSOC)
                        : 0;
        if (space)
        {
            const char *name = base_name(path);
            if (DevEscape(device, ESCAPE_START_DOCUMENT, (LONG)strlen(name), (PVOID)name, NULL,
                          NULL) >= 0)
            {
                result = print_lines(device, space, text, (size_t)size, caps[1]);
                if (DevEscape(device, ESCAPE_END_DOCUMENT, 0, NULL, NULL, NULL) < 0)
                    result = FALSE;
            }
            GpiDestroyPS(space);
        }
        DevCloseDC(device);
    }
    free(text);
    free(token);
    for (LONG i = 0; i < count; i++)
        free(fields[i]);
    return result;
}

static uint32 wpSetFldrFlags(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPFOLDER, self);
    uint8 flags[4];
    lx_guest_store_u32(flags, ARG(a, 1));
    pmwpStore32(data + FOLDER_FLAGS, ARG(a, 1));
    ULONG handle = handle_of(self);
    return !handle || pmwpStoreSetValue(handle, "WPFolder", FOLDER_FLAGS_KEY, flags, sizeof flags);
}

/* An awakened folder has zero instance flags until its saved value is read. */
static uint32 wpQueryFldrFlags(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPFOLDER, self);
    uint32 flags = pmwpLoad32(data + FOLDER_FLAGS);
    uint8 saved[4];
    ULONG size = sizeof saved, handle = handle_of(self);
    if (!flags && handle && pmwpStoreGetValue(handle, "WPFolder", FOLDER_FLAGS_KEY, saved, &size) &&
        size == sizeof saved)
    {
        flags = lx_guest_load_u32(saved);
        pmwpStore32(data + FOLDER_FLAGS, flags);
    }
    return flags;
}

static ULONG *folder_children(uint32 self, size_t *count)
{
    *count = 0;
    char identifier[PATH_LIMIT];
    ULONG handle = handle_of(self);
    if (!handle || !pmwpStoreIdentifier(handle, identifier, sizeof identifier))
        return NULL;
    size_t total = 0;
    ULONG *handles = pmwpStoreList(&total);
    for (size_t i = 0; i < total; i++)
    {
        PmwpRecord record = {0};
        if (!pmwpStoreLoad(handles[i], &record))
            continue;
        if (!strcasecmp(record.location, identifier))
            handles[(*count)++] = handles[i];
        pmwpStoreRelease(&record);
    }
    return handles;
}

/* Directory entries become objects; every child in the store then awakes. */
static uint32 wpPopulate(uint8 *a)
{
    uint32 self = ARG(a, 0);
    BOOL folders_only = ARG(a, 3);
    char path[PATH_LIMIT];
    if (object_file_name(self, path, sizeof path, TRUE) && pmwpPathKind(path) == 2)
    {
        char *host = pmwpHostPath(path);
        LxHostFsError error;
        LxHostDirectory *directory = host ? lx_host_directory_open(host, &error) : NULL;
        const char *name;
        LxHostFileInfo info;
        size_t length = strlen(path);
        while (directory && lx_host_directory_read(directory, &name, &info, &error) > 0)
        {
            if (!strcmp(name, ".") || !strcmp(name, "..") ||
                (folders_only && info.kind != LX_HOST_FILE_DIRECTORY))
                continue;
            char child[PATH_LIMIT];
            snprintf(child, sizeof child, "%s%s%s", path,
                     length && path[length - 1] == '\\' ? "" : "\\", name);
            pmwpObjectForPath(child);
        }
        if (directory)
            lx_host_directory_close(directory);
        free(host);
    }
    size_t count = 0;
    ULONG *children = folder_children(self, &count);
    for (size_t i = 0; i < count; i++)
        pmwpAwaken(children[i]);
    free(children);
    return TRUE;
}

static uint32 wpQueryContent(uint8 *a)
{
    uint32 self = ARG(a, 0), current = ARG(a, 1), option = ARG(a, 2);
    size_t count = 0;
    ULONG *children = folder_children(self, &count);
    uint32 result = 0;
    if (count && option == QC_FIRST_CODE)
        result = pmwpAwaken(children[0]);
    else if (count && option == QC_LAST_CODE)
        result = pmwpAwaken(children[count - 1]);
    else if (option == QC_NEXT_CODE && current)
    {
        ULONG handle = handle_of(current);
        for (size_t i = 0; i + 1 < count; i++)
            if (children[i] == handle)
            {
                result = pmwpAwaken(children[i + 1]);
                break;
            }
    }
    free(children);
    return result;
}

static uint32 wpDeleteContents(uint8 *a)
{
    size_t count = 0;
    ULONG *children = folder_children(ARG(a, 0), &count);
    uint32 result = OK_DELETE_RESULT;
    for (size_t i = 0; i < count; i++)
    {
        uint32 child = pmwpAwaken(children[i]);
        if (!child || pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpDelete, child, ARG(a, 1), 0, 0) !=
                          OK_DELETE_RESULT)
            result = NO_DELETE_RESULT;
    }
    free(children);
    return result;
}

static uint32 parent_method(PmwpClassId id, uint32 offset, uint32 self, uint32 first)
{
    return pmwpGuestCall(pmwpParentMethod(id, pmwpClassToken(PMWP_WPOBJECT, offset)), self, first,
                         0, 0);
}

/* Folder flags are saved here; the parent saves the object and file state. */
static uint32 fdr_wpSaveState(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint8 flags[4];
    lx_guest_store_u32(flags, pmwpLoad32(pmwpInstanceData(PMWP_WPFOLDER, self) + FOLDER_FLAGS));
    return parent_method(PMWP_WPFOLDER, PMWP_WPObject_wpSaveState, self, 0) &&
           pmwpStoreSetValue(handle_of(self), "WPFolder", FOLDER_FLAGS_KEY, flags, sizeof flags);
}

static uint32 fdr_wpRestoreState(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint8 flags[4];
    ULONG size = sizeof flags;
    BOOL result = parent_method(PMWP_WPFOLDER, PMWP_WPObject_wpRestoreState, self, ARG(a, 1));
    if (pmwpStoreGetValue(handle_of(self), "WPFolder", FOLDER_FLAGS_KEY, flags, &size) &&
        size == sizeof flags)
        pmwpStore32(pmwpInstanceData(PMWP_WPFOLDER, self) + FOLDER_FLAGS, lx_guest_load_u32(flags));
    return result;
}

static uint32 folder_view_from_name(const char *name)
{
    return !strcasecmp(name, "ICON")      ? FOLDER_VIEW_ICON
           : !strcasecmp(name, "TREE")    ? FOLDER_VIEW_TREE
           : !strcasecmp(name, "DETAILS") ? FOLDER_VIEW_DETAILS
                                          : 0;
}

/* The folder views are named here; the parent handles every other key. */
static uint32 fdr_wpSetup(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char *setup =
        ARG(a, 1) ? lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), SETUP_LIMIT) : NULL;
    BOOL result = parent_method(PMWP_WPFOLDER, PMWP_WPObject_wpSetup, self, ARG(a, 1));
    char value[32];
    if (result && setup && pmwpSetupValue(setup, "DEFAULTVIEW", value, sizeof value) &&
        folder_view_from_name(value))
        pmwpSetObjectField(self, PMWP_OBJ_DEFAULT_VIEW, folder_view_from_name(value));
    if (result && setup && pmwpSetupValue(setup, "OPEN", value, sizeof value) &&
        folder_view_from_name(value))
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpViewObject, self, 0, folder_view_from_name(value),
                   0);
    free(setup);
    return result;
}

static uint32 dsk_wpSaveState(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint8 drive[4];
    lx_guest_store_u32(drive,
                       pmwpInvoke(PMWP_WPDISK, PMWP_WPDisk_wpQueryLogicalDrive, self, 0, 0, 0));
    return parent_method(PMWP_WPDISK, PMWP_WPObject_wpSaveState, self, 0) &&
           pmwpStoreSetValue(handle_of(self), "WPDisk", DISK_DRIVE_KEY, drive, sizeof drive);
}

static uint32 dsk_wpRestoreState(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint8 drive[4];
    ULONG size = sizeof drive;
    BOOL result = parent_method(PMWP_WPDISK, PMWP_WPObject_wpRestoreState, self, ARG(a, 1));
    if (pmwpStoreGetValue(handle_of(self), "WPDisk", DISK_DRIVE_KEY, drive, &size) &&
        size == sizeof drive)
        pmwpInvoke(PMWP_WPDISK, PMWP_WPDisk_wpSetLogicalDrive, self, lx_guest_load_u32(drive), 0,
                   0);
    return result;
}

static uint32 dsk_wpSetup(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char *setup =
        ARG(a, 1) ? lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), SETUP_LIMIT) : NULL;
    char value[16];
    if (setup && pmwpSetupValue(setup, "DRIVENUM", value, sizeof value))
        pmwpInvoke(PMWP_WPDISK, PMWP_WPDisk_wpSetLogicalDrive, self,
                   (uint32)strtoul(value, NULL, 10), 0, 0);
    free(setup);
    return parent_method(PMWP_WPDISK, PMWP_WPObject_wpSetup, self, ARG(a, 1));
}

/* Contents go first, so a refused child leaves the folder in place. */
static uint32 fdr_wpDelete(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint32 result =
        pmwpInvoke(PMWP_WPFOLDER, PMWP_WPFolder_wpDeleteContents, self, ARG(a, 1), 0, 0);
    return result == OK_DELETE_RESULT ? pmwpObjectDelete(self, ARG(a, 1)) : result;
}

static uint32 fdr_wpRefresh(uint8 *a)
{
    return pmwpInvoke(PMWP_WPFOLDER, PMWP_WPFolder_wpPopulate, ARG(a, 0), 0, 0, FALSE);
}

static void end_conversation(uint32 item, BOOL done)
{
    DrgSendTransferMsg(
        pmwpLoad32(item), DM_ENDCONVERSATION_MESSAGE, (MPARAM)(uintptr_t)pmwpLoad32(item + 4),
        (MPARAM)(uintptr_t)(done ? DMFL_TARGETSUCCESSFUL_FLAG : DMFL_TARGETFAIL_FLAG));
}

/* A folder performs the requested operation on the dragged object. */
static uint32 fdr_wpDrop(uint8 *a)
{
    uint32 self = ARG(a, 0), info = ARG(a, 2), item = ARG(a, 3);
    uint32 source = pmwpAwakeObject(pmwpLoad32(item + 4));
    uint8 bytes[2] = {0};
    lx_execution_read(GLoaderState.execution, info + 6, bytes, sizeof bytes);
    uint32 operation = lx_guest_load_u16(bytes), done = 0;
    if (source && operation == DRAG_MOVE)
        done = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpMoveObject, source, self, 0, 0);
    else if (source && operation == DRAG_LINK)
        done = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpCreateShadowObject, source, self, 0, 0);
    else if (source)
        done = pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpCopyObject, source, self, 0, 0);
    end_conversation(item, done != 0);
    return 1;
}

/* A file dropped on a program starts it with that file. */
static uint32 ref_wpDrop(uint8 *a)
{
    uint32 self = ARG(a, 0), item = ARG(a, 3);
    uint32 source = pmwpAwakeObject(pmwpLoad32(item + 4));
    char path[PATH_LIMIT];
    end_conversation(item, source && object_file_name(source, path, sizeof path, TRUE) &&
                               pmwpStoreStartProgram(handle_of(self), path));
    return 1;
}

/* A drive's views are its root folder's views. */
static uint32 dsk_open_root(uint8 *a)
{
    uint32 root = pmwpInvoke(PMWP_WPDISK, PMWP_WPDisk_wpQueryRootFolder, ARG(a, 0), 0, 0, 0);
    return root ? pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpViewObject, root, ARG(a, 1), ARG(a, 2),
                             ARG(a, 3))
                : 0;
}

static uint32 wpQueryFldrDetailsClass(uint8 *a)
{
    (void)a;
    return pmwpClassObject(PMWP_WPFILESYSTEM);
}

/* A drive object's record names its drive with the DRIVENUM setup key. */
static uint32 wpQueryLogicalDrive(uint8 *a)
{
    uint32 self = ARG(a, 0), data = pmwpInstanceData(PMWP_WPDISK, self);
    uint32 drive = pmwpLoad32(data + DISK_DRIVE);
    PmwpRecord record = {0};
    if (!drive && handle_of(self) && pmwpStoreLoad(handle_of(self), &record))
    {
        char value[16];
        if (pmwpSetupValue(record.setup, "DRIVENUM", value, sizeof value))
        {
            drive = (uint32)strtoul(value, NULL, 10);
            pmwpStore32(data + DISK_DRIVE, drive);
        }
        pmwpStoreRelease(&record);
    }
    return drive;
}

static uint32 wpSetLogicalDrive(uint8 *a)
{
    return pmwpStore32(pmwpInstanceData(PMWP_WPDISK, ARG(a, 0)) + DISK_DRIVE, ARG(a, 1));
}

static uint32 wpQueryRootFolder(uint8 *a)
{
    uint32 drive = pmwpInvoke(PMWP_WPDISK, PMWP_WPDisk_wpQueryLogicalDrive, ARG(a, 0), 0, 0, 0);
    if (!drive || drive > 26)
        return 0;
    char root[] = "A:\\";
    root[0] = (char)('A' + drive - 1);
    return pmwpObjectForPath(root);
}

/* PROGDETAILS is written with its strings packed after the fixed structure. */
static uint32 wpQueryProgDetails(uint8 *a)
{
    uint32 output = ARG(a, 1), size_address = ARG(a, 2);
    ULONG handle = handle_of(ARG(a, 0));
    PmwpRecord record = {0};
    if (!size_address || !handle || !pmwpStoreLoad(handle, &record))
        return FALSE;
    static const char *const keys[] = {"EXENAME", "PARAMETERS", "STARTUPDIR", "ICONFILE"};
    char values[4][PATH_LIMIT];
    uint32 needed = PROGRAM_DETAIL_BYTES + (uint32)strlen(record.title) + 1;
    for (size_t i = 0; i < 4; i++)
    {
        if (!pmwpSetupValue(record.setup, keys[i], values[i], PATH_LIMIT))
            values[i][0] = 0;
        if (values[i][0])
            needed += (uint32)strlen(values[i]) + 1;
    }
    BOOL result = FALSE;
    uint32 capacity = pmwpLoad32(size_address);
    if (!output)
        result = TRUE;
    else if (capacity >= needed)
    {
        uint8 *wire = calloc(1, needed);
        if (wire)
        {
            size_t at = PROGRAM_DETAIL_BYTES;
            lx_guest_store_u32(wire, PROGRAM_DETAIL_BYTES);
            lx_guest_store_u32(wire + 12, output + (uint32)at);
            strcpy((char *)wire + at, record.title);
            at += strlen(record.title) + 1;
            for (size_t i = 0; i < 4; i++)
                if (values[i][0])
                {
                    lx_guest_store_u32(wire + 16 + i * 4, output + (uint32)at);
                    strcpy((char *)wire + at, values[i]);
                    at += strlen(values[i]) + 1;
                }
            result = lx_execution_write(GLoaderState.execution, output, wire, needed);
            free(wire);
        }
    }
    pmwpStore32(size_address, needed);
    pmwpStoreRelease(&record);
    return result;
}

const PmwpBody pmwpClassBodies[] = {
    {"M_WPObject", "somUninit", cls_somUninit, 4},
    {"M_WPObject", "somClassReady", cls_somClassReady, 4},
    {"M_WPObject", "somNew", cls_refuse_creation, 4},
    {"M_WPObject", "somRenew", cls_refuse_creation, 8},
    {"M_WPObject", "wpclsInitData", wpclsInitData, 4},
    {"M_WPObject", "wpclsUnInitData", wpclsUnInitData, 4},
    {"M_WPObject", "wpclsIncUsage", wpclsIncUsage, 4},
    {"M_WPObject", "wpclsDecUsage", wpclsDecUsage, 4},
    {"M_WPObject", "wpclsQueryTitle", title_object, 4},
    {"M_WPAbstract", "wpclsQueryTitle", title_abstract, 4},
    {"M_WPFileSystem", "wpclsQueryTitle", title_file_system, 4},
    {"M_WPFolder", "wpclsQueryTitle", title_folder, 4},
    {"M_WPDataFile", "wpclsQueryTitle", title_data_file, 4},
    {"M_WPTransient", "wpclsQueryTitle", title_transient, 4},
    {"M_WPDesktop", "wpclsQueryTitle", title_desktop, 4},
    {"M_WPDisk", "wpclsQueryTitle", title_disk, 4},
    {"M_WPProgram", "wpclsQueryTitle", title_program, 4},
    {"M_WPShadow", "wpclsQueryTitle", title_shadow, 4},
    {"M_WPObject", "wpclsQueryIcon", wpclsQueryIcon, 4},
    {"M_WPObject", "wpclsQueryStyle", wpclsQueryStyle, 4},
    {"M_WPFolder", "wpclsQueryStyle", style_folder, 4},
    {"M_WPDataFile", "wpclsQueryStyle", style_data_file, 4},
    {"M_WPTransient", "wpclsQueryStyle", style_transient, 4},
    {"M_WPDesktop", "wpclsQueryStyle", style_desktop, 4},
    {"M_WPDisk", "wpclsQueryStyle", style_disk, 4},
    {"M_WPProgram", "wpclsQueryStyle", style_program, 4},
    {"M_WPShadow", "wpclsQueryStyle", style_shadow, 4},
    {"M_WPFolder", "wpclsQueryDefaultView", view_folder, 4},
    {"M_WPDataFile", "wpclsQueryDefaultView", view_data_file, 4},
    {"M_WPDesktop", "wpclsQueryDefaultView", view_desktop, 4},
    {"M_WPDisk", "wpclsQueryDefaultView", view_disk, 4},
    {"M_WPProgram", "wpclsQueryDefaultView", view_program, 4},
    {"M_WPFolder", "wpclsQueryDefaultHelp", help_folder, 12},
    {"M_WPDataFile", "wpclsQueryDefaultHelp", help_data_file, 12},
    {"M_WPDesktop", "wpclsQueryDefaultHelp", help_desktop, 12},
    {"M_WPDisk", "wpclsQueryDefaultHelp", help_disk, 12},
    {"M_WPProgram", "wpclsQueryDefaultHelp", help_program, 12},
    {"M_WPFolder", "wpclsQueryMaxAnimationIcons", folder_animation_icons, 4},
    {"M_WPFileSystem", "wpclsQuerySettingsPageSize", page_size_120, 8},
    {"M_WPFolder", "wpclsQuerySettingsPageSize", page_size_120, 8},
    {"M_WPDataFile", "wpclsQuerySettingsPageSize", page_size_120, 8},
    {"M_WPDesktop", "wpclsQuerySettingsPageSize", page_size_120, 8},
    {"M_WPProgram", "wpclsQuerySettingsPageSize", page_size_125, 8},
    {"WPFileSystem", "wpCopyObject", fs_wpCopyObject, 12},
    {"WPFileSystem", "wpMoveObject", fs_wpMoveObject, 8},
    {"WPFileSystem", "wpSetTitle", fs_wpSetTitle, 8},
    {"WPFileSystem", "wpQueryStyle", fs_wpQueryStyle, 4},
    {"WPFileSystem", "wpAddSettingsPages", fs_wpAddSettingsPages, 8},
    {"WPFolder", "wpSaveState", fdr_wpSaveState, 4},
    {"WPFolder", "wpRestoreState", fdr_wpRestoreState, 8},
    {"WPFolder", "wpSetup", fdr_wpSetup, 8},
    {"WPDisk", "wpSaveState", dsk_wpSaveState, 4},
    {"WPDisk", "wpRestoreState", dsk_wpRestoreState, 8},
    {"WPDisk", "wpSetup", dsk_wpSetup, 8},
    {"WPFolder", "wpDelete", fdr_wpDelete, 8},
    {"WPFolder", "wpRefresh", fdr_wpRefresh, 12},
    {"WPFolder", "wpDrop", fdr_wpDrop, 16},
    {"WPProgram", "wpDrop", ref_wpDrop, 16},
    {"WPDisk", "wpOpen", dsk_open_root, 16},
    {"WPDisk", "wpViewObject", dsk_open_root, 16},
    {"M_WPObject", "wpclsQueryDefaultView", wpclsQueryDefaultView, 4},
    {"M_WPObject", "wpclsCreateDefaultTemplates", wpclsCreateDefaultTemplates, 8},
    {"M_WPObject", "wpclsQueryDefaultHelp", wpclsQueryDefaultHelp, 12},
    {"M_WPObject", "wpclsQueryDetailsInfo", wpclsQueryDetailsInfo, 12},
    {"M_WPObject", "wpclsSetError", wpclsSetError, 8},
    {"M_WPObject", "wpclsQueryError", wpclsQueryError, 4},
    {"M_WPObject", "wpclsQuerySettingsPageSize", wpclsQuerySettingsPageSize, 8},
    {"M_WPObject", "wpclsNew", wpclsNew, 20},
    {"M_WPObject", "wpclsINew", wpclsNew, 24},
    {"M_WPObject", "wpclsQueryFolder", wpclsQueryFolder, 12},
    {"M_WPObject", "wpclsObjectFromHandle", wpclsObjectFromHandle, 8},
    {"M_WPObject", "wpclsQueryObject", wpclsObjectFromHandle, 8},
    {"M_WPObject", "wpclsFindObjectFirst", wpclsFindObjectFirst, 36},
    {"M_WPObject", "wpclsFindObjectNext", wpclsFindObjectNext, 16},
    {"M_WPObject", "wpclsFindObjectEnd", wpclsFindObjectEnd, 8},
    {"M_WPFileSystem", "wpclsQueryInstanceType", file_empty_string, 4},
    {"M_WPFileSystem", "wpclsQueryInstanceFilter", file_empty_string, 4},
    {"M_WPFileSystem", "wpclsQueryObjectFromPath", wpclsQueryObjectFromPath, 8},
    {"WPFileSystem", "wpQueryRealName", wpQueryRealName, 16},
    {"WPFileSystem", "wpQueryFilename", wpQueryFilename, 12},
    {"WPFileSystem", "wpRefresh", wpRefresh, 12},
    {"WPFileSystem", "wpSetType", wpSetType, 12},
    {"WPFileSystem", "wpQueryType", wpQueryType, 4},
    {"WPFileSystem", "wpFormatDragItem", fs_wpFormatDragItem, 8},
    {"WPFileSystem", "wpDroppedOnObject", fs_wpDroppedOnObject, 8},
    {"WPFileSystem", "wpPrintPlainTextFile", df_wpPrintObject, 8},
    {"WPDataFile", "wpOpen", df_wpOpen, 16},
    {"WPDataFile", "wpPrintObject", df_wpPrintObject, 12},
    {"WPDataFile", "wpQueryAssociatedProgram", wpQueryAssociatedProgram, 24},
    {"WPDataFile", "wpQueryAssociatedFileIcon", wpQueryAssociatedFileIcon, 4},
    {"WPDataFile", "wpSetAssociatedFileIcon", wpSetAssociatedFileIcon, 4},
    {"WPFolder", "wpSetFldrFlags", wpSetFldrFlags, 8},
    {"WPFolder", "wpQueryFldrFlags", wpQueryFldrFlags, 4},
    {"WPFolder", "wpPopulate", wpPopulate, 16},
    {"WPFolder", "wpQueryContent", wpQueryContent, 12},
    {"WPFolder", "wpDeleteContents", wpDeleteContents, 8},
    {"WPFolder", "wpQueryFldrDetailsClass", wpQueryFldrDetailsClass, 4},
    {"WPDisk", "wpQueryLogicalDrive", wpQueryLogicalDrive, 4},
    {"WPDisk", "wpSetLogicalDrive", wpSetLogicalDrive, 8},
    {"WPDisk", "wpQueryRootFolder", wpQueryRootFolder, 4},
    {"WPProgram", "wpQueryProgDetails", wpQueryProgDetails, 12},
};

const size_t pmwpClassBodyCount = sizeof pmwpClassBodies / sizeof *pmwpClassBodies;
