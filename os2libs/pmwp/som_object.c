/* Public domain. No rights reserved. Wholly AI-authored. */
/* WPObject lifetime, state, persistence and use-list behaviour. Everything an object
   owns lives in its guest instance data, so it is released with the object; state
   that survives the object is kept in the persistent store under its handle. */
#include "os2libs/pmwp/internal.h"
#include "guest/abi.h"
#include "hal/host_sync.h"
#include <strings.h>

#define ARG(a, n) lx_guest_load_u32((a) + 4 * (n))
enum
{
    TITLE_LIMIT = 4097,
    SETUP_LIMIT = 32768,
    CLASS_LIMIT = 256,
    VALUE_LIMIT = 65536,
    ERROR_NOT_ENOUGH_MEMORY_CODE = 8,
    OK_DELETE = 1,
    NO_DELETE = 2,
    STATE_STYLE_KEY = 1,
    INIT_CONTROL_MASK = 0,
    INIT_CONTROL_INFO = 4,
    INIT_CONTROL_SIZE = 8
};

typedef struct AwakeObject
{
    ULONG handle;
    uint32 object;
} AwakeObject;
static LxHostOnce awake_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *awake_lock;
static AwakeObject *awake;
static size_t awake_count;

static void initialize_awake(void)
{
    awake_lock = lx_host_monitor_create();
    if (!awake_lock)
        abort();
}
static void lock_awake(void)
{
    lx_host_once(&awake_once, initialize_awake);
    lx_host_monitor_lock(awake_lock);
}
uint32 pmwpAwakeObject(ULONG handle)
{
    uint32 result = 0;
    lock_awake();
    for (size_t i = 0; i < awake_count; i++)
        if (awake[i].handle == handle)
            result = awake[i].object;
    lx_host_monitor_unlock(awake_lock);
    return result;
}
void pmwpRememberObject(ULONG handle, uint32 object)
{
    lock_awake();
    AwakeObject *next = realloc(awake, (awake_count + 1) * sizeof *awake);
    if (next)
    {
        awake = next;
        awake[awake_count++] = (AwakeObject){handle, object};
    }
    lx_host_monitor_unlock(awake_lock);
}
void pmwpForgetObject(ULONG handle)
{
    lock_awake();
    for (size_t i = 0; i < awake_count; i++)
        if (awake[i].handle == handle)
        {
            awake[i] = awake[--awake_count];
            break;
        }
    lx_host_monitor_unlock(awake_lock);
}

static uint32 class_of(uint32 self)
{
    return pmwpInvokeObject(PMWP_SOMObject_somGetClass, self, 0, 0, 0);
}
static void replace_string(uint32 self, uint32 field, const char *text)
{
    pmwpFree(pmwpObjectField(self, field));
    pmwpSetObjectField(self, field, text ? pmwpGuestString(text) : 0);
}

/* Advances the guest initializer or destructor cursor past the SOMObject parent.
   WPObject's only parent is the root, which has no implementation to run here. */
static void advance_control(uint32 control)
{
    if (!control)
        return;
    uint32 mask = pmwpLoad32(control + INIT_CONTROL_MASK);
    uint32 info = pmwpLoad32(control + INIT_CONTROL_INFO);
    uint32 size = pmwpLoad32(control + INIT_CONTROL_SIZE);
    uint8 flags = 0;
    lx_execution_read(GLoaderState.execution, mask, &flags, 1);
    info += size;
    if (flags & 1)
        info += size;
    pmwpStore32(control + INIT_CONTROL_MASK, mask + 1);
    pmwpStore32(control + INIT_CONTROL_INFO, info);
}
static void uninitialize(uint32 self)
{
    uint32 object_class = class_of(self);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpUnInitData, self, 0, 0, 0);
    pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsDecUsage, object_class, 0, 0, 0);
}

static uint32 obj_somInit(uint8 *a)
{
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpInitData, ARG(a, 0), 0, 0, 0);
    return 0;
}
static uint32 obj_somDefaultInit(uint8 *a)
{
    advance_control(ARG(a, 1));
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpInitData, ARG(a, 0), 0, 0, 0);
    return 0;
}
static uint32 obj_somUninit(uint8 *a)
{
    uninitialize(ARG(a, 0));
    return 0;
}
static uint32 obj_somDestruct(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uninitialize(self);
    advance_control(ARG(a, 2));
    if (ARG(a, 1) & 0xff)
        pmwpInvokeClass(PMWP_SOMClass_somDeallocate, pmwpClassObject(PMWP_WPOBJECT), self, 0, 0);
    return 0;
}

static uint32 wpInitData(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint32 style = pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsQueryStyle, class_of(self), 0,
                              0, 0);
    /* Class styles NEVERTEMPLATE and PRIVATE share bits with no instance meaning. */
    pmwpSetObjectField(self, PMWP_OBJ_STYLE, style & 0xffffefefu);
    pmwpSetObjectField(self, PMWP_OBJ_CONCURRENT_VIEW, 0);
    return 0;
}
static uint32 wpUnInitData(uint8 *a)
{
    uint32 self = ARG(a, 0);
    uint32 data = pmwpInstanceData(PMWP_WPOBJECT, self);
    for (uint32 item = pmwpLoad32(data + PMWP_OBJ_USE_LIST); item;)
    {
        uint32 next = pmwpLoad32(item + 4);
        if (pmwpLoad32(item) == PMWP_USAGE_MEMORY)
            pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpFreeMem, self, item + 12, 0, 0);
        item = next;
    }
    ULONG handle = pmwpLoad32(data + PMWP_OBJ_HANDLE);
    if (handle)
        pmwpForgetObject(handle);
    pmwpFree(pmwpLoad32(data + PMWP_OBJ_TITLE));
    pmwpFree(pmwpLoad32(data + PMWP_OBJ_IDENTIFIER));
    pmwpStore32(data + PMWP_OBJ_TITLE, 0);
    pmwpStore32(data + PMWP_OBJ_IDENTIFIER, 0);
    return 0;
}

static uint32 wpSetTitle(uint8 *a)
{
    uint32 self = ARG(a, 0);
    char *title = lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), TITLE_LIMIT);
    if (!title)
        return FALSE;
    replace_string(self, PMWP_OBJ_TITLE, title);
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    if (handle > 1)
        pmwpStoreSetTitle(handle, title);
    free(title);
    return pmwpObjectField(self, PMWP_OBJ_TITLE) != 0;
}
static uint32 wpQueryTitle(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_TITLE);
}
static uint32 wpSetStyle(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_STYLE, ARG(a, 1));
    return TRUE;
}
static uint32 wpQueryStyle(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_STYLE);
}
static uint32 wpModifyStyle(uint8 *a)
{
    uint32 self = ARG(a, 0), mask = ARG(a, 1);
    uint32 style = pmwpObjectField(self, PMWP_OBJ_STYLE);
    pmwpSetObjectField(self, PMWP_OBJ_STYLE, (style & ~mask) | (ARG(a, 2) & mask));
    return TRUE;
}
static uint32 wpSetFolder(uint8 *a)
{
    pmwpSetObjectField(ARG(a, 0), PMWP_OBJ_FOLDER, ARG(a, 1));
    return TRUE;
}
static uint32 wpQueryFolder(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_FOLDER);
}
static uint32 wpQueryHandle(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_HANDLE);
}
static uint32 wpQueryObjectID(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_IDENTIFIER);
}
static BOOL assign_identifier(uint32 self, const char *identifier)
{
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    ULONG owner = pmwpStoreFind(identifier);
    if (owner && owner != handle)
        return FALSE;
    replace_string(self, PMWP_OBJ_IDENTIFIER, identifier);
    return TRUE;
}
static uint32 wpSetObjectID(uint8 *a)
{
    char *identifier = lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), 1024);
    BOOL result = identifier && assign_identifier(ARG(a, 0), identifier);
    if (result)
    {
        ULONG handle = pmwpObjectField(ARG(a, 0), PMWP_OBJ_HANDLE);
        char setup[1040];
        snprintf(setup, sizeof setup, "OBJECTID=%s;", identifier);
        if (handle > 1)
            result = pmwpStoreAppendSetup(handle, setup);
    }
    free(identifier);
    return result;
}
static uint32 wpLockObject(uint8 *a)
{
    uint32 self = ARG(a, 0);
    pmwpSetObjectField(self, PMWP_OBJ_LOCKS, pmwpObjectField(self, PMWP_OBJ_LOCKS) + 1);
    return 0;
}
static uint32 wpUnlockObject(uint8 *a)
{
    uint32 self = ARG(a, 0), locks = pmwpObjectField(self, PMWP_OBJ_LOCKS);
    if (locks)
        pmwpSetObjectField(self, PMWP_OBJ_LOCKS, locks - 1);
    return TRUE;
}
static uint32 wpIsLocked(uint8 *a)
{
    return pmwpObjectField(ARG(a, 0), PMWP_OBJ_LOCKS) != 0;
}

/* Instance state is saved under the object's handle. An object without a handle is
   transient: it has nowhere to persist, so saves report failure. */
static BOOL save_value(uint32 self, uint32 class_name, ULONG key, const void *data, ULONG size)
{
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    char *name = lx_execution_copy_string(GLoaderState.execution, class_name, CLASS_LIMIT);
    BOOL result = handle && name && pmwpStoreSetValue(handle, name, key, data, size);
    free(name);
    return result;
}
static BOOL restore_value(uint32 self, uint32 class_name, ULONG key, void *data, ULONG *size)
{
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    char *name = lx_execution_copy_string(GLoaderState.execution, class_name, CLASS_LIMIT);
    BOOL result = handle && name && pmwpStoreGetValue(handle, name, key, data, size);
    free(name);
    return result;
}
static uint32 wpSaveLong(uint8 *a)
{
    uint8 value[4];
    lx_guest_store_u32(value, ARG(a, 3));
    return save_value(ARG(a, 0), ARG(a, 1), ARG(a, 2), value, sizeof value);
}
static uint32 wpSaveString(uint8 *a)
{
    if (!ARG(a, 3))
        return FALSE;
    char *value = lx_execution_copy_string(GLoaderState.execution, ARG(a, 3), VALUE_LIMIT);
    BOOL result =
        value && save_value(ARG(a, 0), ARG(a, 1), ARG(a, 2), value, (ULONG)strlen(value) + 1);
    free(value);
    return result;
}
static uint32 wpSaveData(uint8 *a)
{
    uint32 size = ARG(a, 4);
    if (size > VALUE_LIMIT)
        return FALSE;
    uint8 *value = malloc(size ? size : 1);
    BOOL result = value &&
                  (!size || lx_execution_read(GLoaderState.execution, ARG(a, 3), value, size)) &&
                  save_value(ARG(a, 0), ARG(a, 1), ARG(a, 2), value, size);
    free(value);
    return result;
}
static uint32 wpRestoreLong(uint8 *a)
{
    uint8 value[4] = {0};
    ULONG size = sizeof value;
    return restore_value(ARG(a, 0), ARG(a, 1), ARG(a, 2), value, &size) && size == sizeof value &&
           pmwpStore32(ARG(a, 3), lx_guest_load_u32(value));
}
/* A null buffer asks for the stored size; a short buffer receives the leading bytes. */
static uint32 restore_bytes(uint8 *a)
{
    uint32 output = ARG(a, 3), size_address = ARG(a, 4);
    if (!size_address)
        return FALSE;
    ULONG size = pmwpLoad32(size_address);
    if (!output)
    {
        if (!restore_value(ARG(a, 0), ARG(a, 1), ARG(a, 2), NULL, &size))
            return FALSE;
        return pmwpStore32(size_address, size);
    }
    if (size > VALUE_LIMIT)
        size = VALUE_LIMIT;
    uint8 *value = malloc(size ? size : 1);
    BOOL result = value && restore_value(ARG(a, 0), ARG(a, 1), ARG(a, 2), value, &size) &&
                  lx_execution_write(GLoaderState.execution, output, value, size) &&
                  pmwpStore32(size_address, size);
    free(value);
    return result;
}
static uint32 wpSaveState(uint8 *a)
{
    uint32 self = ARG(a, 0);
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    if (!handle)
        return FALSE;
    uint8 style[4];
    lx_guest_store_u32(style, pmwpObjectField(self, PMWP_OBJ_STYLE));
    return pmwpStoreSetValue(handle, "WPObject", STATE_STYLE_KEY, style, sizeof style);
}
static uint32 wpRestoreState(uint8 *a)
{
    uint32 self = ARG(a, 0);
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    if (!handle)
        return FALSE;
    uint8 style[4];
    ULONG size = sizeof style;
    if (pmwpStoreGetValue(handle, "WPObject", STATE_STYLE_KEY, style, &size) && size == sizeof style)
        pmwpSetObjectField(self, PMWP_OBJ_STYLE, lx_guest_load_u32(style));
    PmwpRecord record = {0};
    if (pmwpStoreLoad(handle, &record))
    {
        char identifier[1024];
        replace_string(self, PMWP_OBJ_TITLE, record.title);
        if (pmwpSetupValue(record.setup, "OBJECTID", identifier, sizeof identifier))
            replace_string(self, PMWP_OBJ_IDENTIFIER, identifier);
        pmwpStoreRelease(&record);
    }
    return TRUE;
}
static uint32 wpSaveNow(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSaveState, ARG(a, 0), 0, 0, 0);
}

/* USEITEM is {type, next}; a memory item adds its size, and the caller's block follows
   the 12-byte header. */
static uint32 wpAddToObjUseList(uint8 *a)
{
    uint32 self = ARG(a, 0), item = ARG(a, 1);
    if (!item || pmwpLoad32(item) > PMWP_USAGE_LAST)
        return FALSE;
    uint32 data = pmwpInstanceData(PMWP_WPOBJECT, self);
    pmwpStore32(item + 4, 0);
    uint32 link = data + PMWP_OBJ_USE_LIST;
    for (uint32 at = pmwpLoad32(link); at; at = pmwpLoad32(link))
    {
        if (at == item)
            return TRUE;
        link = at + 4;
    }
    return pmwpStore32(link, item);
}
static uint32 wpDeleteFromObjUseList(uint8 *a)
{
    uint32 item = ARG(a, 1);
    uint32 link = pmwpInstanceData(PMWP_WPOBJECT, ARG(a, 0)) + PMWP_OBJ_USE_LIST;
    for (uint32 at = pmwpLoad32(link); at; at = pmwpLoad32(link))
    {
        if (at == item)
            return pmwpStore32(link, pmwpLoad32(item + 4));
        link = at + 4;
    }
    return FALSE;
}
static uint32 wpFindUseItem(uint8 *a)
{
    uint32 type = ARG(a, 1), current = ARG(a, 2);
    uint32 head = pmwpObjectField(ARG(a, 0), PMWP_OBJ_USE_LIST), at = head;
    if (current)
    {
        while (at && at != current)
            at = pmwpLoad32(at + 4);
        if (!at)
            return 0;
        at = pmwpLoad32(at + 4);
    }
    for (; at; at = pmwpLoad32(at + 4))
        if (pmwpLoad32(at) == type)
            return at;
    return 0;
}
static uint32 wpAllocMem(uint8 *a)
{
    uint32 self = ARG(a, 0), size = ARG(a, 1), status = ARG(a, 2);
    if (!size)
        return 0;
    uint32 item = size <= UINT32_MAX - 12 ? pmwpMalloc(size + 12) : 0;
    if (!item)
    {
        if (status)
            pmwpStore32(status, ERROR_NOT_ENOUGH_MEMORY_CODE);
        return 0;
    }
    pmwpStore32(item, PMWP_USAGE_MEMORY);
    pmwpStore32(item + 8, size);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpAddToObjUseList, self, item, 0, 0);
    return item + 12;
}
static uint32 wpFreeMem(uint8 *a)
{
    uint32 block = ARG(a, 1);
    if (block < 12 ||
        !pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpDeleteFromObjUseList, ARG(a, 0), block - 12, 0, 0))
        return FALSE;
    pmwpFree(block - 12);
    return TRUE;
}

static uint32 wpScanSetupString(uint8 *a)
{
    uint32 output = ARG(a, 3), size_address = ARG(a, 4);
    char *setup = lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), SETUP_LIMIT);
    char *key = lx_execution_copy_string(GLoaderState.execution, ARG(a, 2), CLASS_LIMIT);
    char *value = malloc(SETUP_LIMIT);
    BOOL result = FALSE;
    if (setup && key && value && *setup && size_address &&
        pmwpSetupValue(setup, key, value, SETUP_LIMIT))
    {
        ULONG needed = (ULONG)strlen(value) + 1;
        ULONG capacity = pmwpLoad32(size_address);
        if (!output)
            result = TRUE;
        else if (needed <= capacity)
            result = lx_execution_write(GLoaderState.execution, output, value, needed);
        pmwpStore32(size_address, needed);
    }
    free(setup);
    free(key);
    free(value);
    return result;
}

static BOOL yes_value(const char *value)
{
    return !strcasecmp(value, "YES");
}
static void apply_style_key(uint32 self, const char *setup, const char *key, uint32 bit)
{
    char value[8];
    if (!pmwpSetupValue(setup, key, value, sizeof value))
        return;
    uint32 style = pmwpObjectField(self, PMWP_OBJ_STYLE);
    pmwpSetObjectField(self, PMWP_OBJ_STYLE, yes_value(value) ? style | bit : style & ~bit);
}
/* Base setup keys. Classes parse their own keys in their overrides and call this
   implementation through their parent. Updates persist with the object's record. */
static uint32 wpSetup(uint8 *a)
{
    static const struct
    {
        const char *key;
        uint32 bit;
    } styles[] = {{"NOMOVE", PMWP_OBJSTYLE_NOMOVE},
                  {"NOLINK", PMWP_OBJSTYLE_NOLINK},
                  {"NOCOPY", PMWP_OBJSTYLE_NOCOPY},
                  {"NODELETE", PMWP_OBJSTYLE_NODELETE},
                  {"NOPRINT", PMWP_OBJSTYLE_NOPRINT},
                  {"NODRAG", PMWP_OBJSTYLE_NODRAG},
                  {"NOTVISIBLE", PMWP_OBJSTYLE_NOTVISIBLE},
                  {"NOSETTINGS", PMWP_OBJSTYLE_NOSETTINGS},
                  {"NORENAME", PMWP_OBJSTYLE_NORENAME},
                  {"NODROP", PMWP_OBJSTYLE_NODROP},
                  {"TEMPLATE", PMWP_OBJSTYLE_TEMPLATE},
                  {"LOCKPOSITION", PMWP_OBJSTYLE_LOCKEDINPLACE}};
    uint32 self = ARG(a, 0);
    if (!ARG(a, 1))
        return TRUE;
    char *setup = lx_execution_copy_string(GLoaderState.execution, ARG(a, 1), SETUP_LIMIT);
    if (!setup)
        return FALSE;
    BOOL result = TRUE;
    char value[TITLE_LIMIT];
    if (pmwpSetupValue(setup, "TITLE", value, sizeof value))
    {
        uint32 title = pmwpGuestString(value);
        result = title && pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetTitle, self, title, 0, 0);
        pmwpFree(title);
    }
    if (result && pmwpSetupValue(setup, "OBJECTID", value, sizeof value))
        result = assign_identifier(self, value);
    for (size_t i = 0; i < sizeof styles / sizeof *styles; i++)
        apply_style_key(self, setup, styles[i].key, styles[i].bit);
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    if (result && handle > 1 && *setup)
        result = pmwpStoreAppendSetup(handle, setup);
    if (result && pmwpSetupValue(setup, "OPEN", value, sizeof value))
    {
        ULONG view = !strcasecmp(value, "SETTINGS") ? PMWP_OPEN_SETTINGS : PMWP_OPEN_DEFAULT;
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpViewObject, self, 0, view, 0);
    }
    free(setup);
    return result;
}
static uint32 wpSetupOnce(uint8 *a)
{
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetup, ARG(a, 0), ARG(a, 1), 0, 0);
}

static uint32 wpFree(uint8 *a)
{
    uint32 self = ARG(a, 0);
    ULONG handle = pmwpObjectField(self, PMWP_OBJ_HANDLE);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpClose, self, 0, 0, 0);
    if (handle > 1)
        pmwpStoreDelete(handle);
    /* SOMObject::somFree runs somDestruct, which releases the instance. */
    pmwpInvokeObject(0x0c, self, 0, 0, 0);
    return TRUE;
}
static uint32 wpDelete(uint8 *a)
{
    uint32 self = ARG(a, 0);
    if (pmwpObjectField(self, PMWP_OBJ_STYLE) & PMWP_OBJSTYLE_NODELETE)
        return NO_DELETE;
    return pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpFree, self, 0, 0, 0) ? OK_DELETE : NO_DELETE;
}
static uint32 wpIsDeleteable(uint8 *a)
{
    return !(pmwpObjectField(ARG(a, 0), PMWP_OBJ_STYLE) & PMWP_OBJSTYLE_NODELETE);
}

/* The allocation and construction steps of wpclsINew. somNew is disabled for these
   classes, so the first parent's somRenew initializes the block. */
static uint32 allocate_object(uint32 class_object)
{
    uint32 size = pmwpInvokeClass(PMWP_SOMClass_somGetInstanceSize, class_object, 0, 0, 0);
    uint32 block = size ? pmwpCalloc(size) : 0;
    if (!block)
        return 0;
    uint32 object = pmwpGuestCall(pmwpParentProc(PMWP_M_WPOBJECT, PMWP_SOMClass_somRenew),
                                  class_object, block, 0, 0);
    if (!object)
        pmwpFree(block);
    return object;
}
/* The record starts with an empty setup: wpSetup appends the string it accepts. */
static void persist_new_object(uint32 object, uint32 class_object, uint32 folder)
{
    /* WPTransient and its subclasses are never saved. */
    uint32 transient = pmwpClassObject(PMWP_WPTRANSIENT);
    if (transient && pmwpInvokeClass(PMWP_SOMClass_somDescendedFrom, class_object, transient, 0, 0))
        return;
    uint32 name = pmwpInvokeClass(PMWP_SOMClass_somGetName, class_object, 0, 0, 0);
    char *class_name = lx_execution_copy_string(GLoaderState.execution, name, CLASS_LIMIT);
    char *title = lx_execution_copy_string(
        GLoaderState.execution, pmwpObjectField(object, PMWP_OBJ_TITLE), TITLE_LIMIT);
    char location[1024] = "<WP_DESKTOP>";
    ULONG parent = folder ? pmwpObjectField(folder, PMWP_OBJ_HANDLE) : 0;
    if (parent)
        pmwpStoreIdentifier(parent, location, sizeof location);
    if (class_name && title)
    {
        ULONG handle = pmwpStoreCreate(class_name, title, location, "");
        if (handle)
        {
            pmwpSetObjectField(object, PMWP_OBJ_HANDLE, handle);
            pmwpRememberObject(handle, object);
        }
    }
    free(class_name);
    free(title);
}
uint32 pmwpNewObject(uint32 class_object, uint32 title, uint32 setup, uint32 folder, BOOL lock)
{
    uint32 object = allocate_object(class_object);
    if (!object)
        return 0;
    uint8 first = 0;
    if (title)
        lx_execution_read(GLoaderState.execution, title, &first, 1);
    if (!first)
        title = pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsQueryTitle, class_object, 0, 0, 0);
    pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsIncUsage, class_object, 0, 0, 0);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetFolder, object, folder, 0, 0);
    if (!pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetTitle, object, title, 0, 0))
        goto fail;
    persist_new_object(object, class_object, folder);
    if (!pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetupOnce, object, setup, 0, 0))
        goto fail;
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSaveImmediate, object, 0, 0, 0);
    if (lock)
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpLockObject, object, 0, 0, 0);
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpObjectReady, object, PMWP_OR_NEW, 0, 0);
    return object;
fail:
    pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpFree, object, 0, 0, 0);
    return 0;
}

/* Recreates the object for a store record. It is published before its folder awakes
   so a damaged location cycle ends at the existing entry. */
uint32 pmwpAwaken(ULONG handle)
{
    uint32 object = pmwpAwakeObject(handle);
    if (object)
        return object;
    PmwpRecord record = {0};
    char desktop_class[] = "WPDesktop", empty[] = "";
    if (handle == 1)
        record.class_name = desktop_class, record.location = empty;
    else if (!pmwpStoreLoad(handle, &record))
        return 0;
    uint32 class_object = pmwpClassForName(record.class_name);
    object = class_object ? allocate_object(class_object) : 0;
    if (object)
    {
        pmwpInvoke(PMWP_M_WPOBJECT, PMWP_M_WPObject_wpclsIncUsage, class_object, 0, 0, 0);
        pmwpSetObjectField(object, PMWP_OBJ_HANDLE, handle);
        pmwpRememberObject(handle, object);
        if (*record.location)
            pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpSetFolder, object,
                       pmwpFolderForLocation(record.location), 0, 0);
        if (handle == 1)
            replace_string(object, PMWP_OBJ_TITLE, "Desktop");
        else
            pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpRestoreState, object, 0, 0, 0);
        pmwpInvoke(PMWP_WPOBJECT, PMWP_WPObject_wpObjectReady, object, PMWP_OR_AWAKE, 0, 0);
    }
    pmwpStoreRelease(&record);
    return object;
}

const PmwpBody pmwpObjectBodies[] = {
    {"WPObject", "somInit", obj_somInit, 4},
    {"WPObject", "somDefaultInit", obj_somDefaultInit, 8},
    {"WPObject", "somUninit", obj_somUninit, 4},
    {"WPObject", "somDestruct", obj_somDestruct, 12},
    {"WPObject", "wpInitData", wpInitData, 4},
    {"WPObject", "wpUnInitData", wpUnInitData, 4},
    {"WPObject", "wpSetTitle", wpSetTitle, 8},
    {"WPObject", "wpQueryTitle", wpQueryTitle, 4},
    {"WPObject", "wpSetStyle", wpSetStyle, 8},
    {"WPObject", "wpQueryStyle", wpQueryStyle, 4},
    {"WPObject", "wpModifyStyle", wpModifyStyle, 12},
    {"WPObject", "wpSetFolder", wpSetFolder, 8},
    {"WPObject", "wpQueryFolder", wpQueryFolder, 4},
    {"WPObject", "wpQueryHandle", wpQueryHandle, 4},
    {"WPObject", "wpQueryObjectID", wpQueryObjectID, 4},
    {"WPObject", "wpSetObjectID", wpSetObjectID, 8},
    {"WPObject", "wpLockObject", wpLockObject, 4},
    {"WPObject", "wpUnlockObject", wpUnlockObject, 4},
    {"WPObject", "wpIsLocked", wpIsLocked, 4},
    {"WPObject", "wpSaveLong", wpSaveLong, 16},
    {"WPObject", "wpSaveString", wpSaveString, 16},
    {"WPObject", "wpSaveData", wpSaveData, 20},
    {"WPObject", "wpRestoreLong", wpRestoreLong, 16},
    {"WPObject", "wpRestoreString", restore_bytes, 20},
    {"WPObject", "wpRestoreData", restore_bytes, 20},
    {"WPObject", "wpSaveState", wpSaveState, 4},
    {"WPObject", "wpRestoreState", wpRestoreState, 8},
    {"WPObject", "wpSaveImmediate", wpSaveNow, 4},
    {"WPObject", "wpSaveDeferred", wpSaveNow, 4},
    {"WPObject", "wpAddToObjUseList", wpAddToObjUseList, 8},
    {"WPObject", "wpDeleteFromObjUseList", wpDeleteFromObjUseList, 8},
    {"WPObject", "wpFindUseItem", wpFindUseItem, 12},
    {"WPObject", "wpAllocMem", wpAllocMem, 12},
    {"WPObject", "wpFreeMem", wpFreeMem, 8},
    {"WPObject", "wpScanSetupString", wpScanSetupString, 20},
    {"WPObject", "wpSetup", wpSetup, 8},
    {"WPObject", "wpSetupOnce", wpSetupOnce, 8},
    {"WPObject", "wpFree", wpFree, 4},
    {"WPObject", "wpDelete", wpDelete, 8},
    {"WPObject", "wpIsDeleteable", wpIsDeleteable, 4},
};
const size_t pmwpObjectBodyCount = sizeof pmwpObjectBodies / sizeof *pmwpObjectBodies;
