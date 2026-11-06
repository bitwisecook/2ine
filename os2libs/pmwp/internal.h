/* Public domain. No rights reserved. Wholly AI-authored. */
/* Workplace Shell classes built on the guest SOM runtime, and the persistent
   object store that backs them. Addresses of type uint32 are guest addresses. */
#ifndef TWOINE_PMWP_INTERNAL_H
#define TWOINE_PMWP_INTERNAL_H
#include "os2libs/os2native.h"
#include "os2libs/pmwp.h"
#include "os2libs/pmwp/som_tokens.h"

typedef enum PmwpClassId
{
    PMWP_NO_CLASS = -1,
    PMWP_M_WPOBJECT,
    PMWP_WPOBJECT,
    PMWP_M_WPABSTRACT,
    PMWP_WPABSTRACT,
    PMWP_M_WPFILESYSTEM,
    PMWP_WPFILESYSTEM,
    PMWP_M_WPFOLDER,
    PMWP_WPFOLDER,
    PMWP_M_WPDATAFILE,
    PMWP_WPDATAFILE,
    PMWP_M_WPTRANSIENT,
    PMWP_WPTRANSIENT,
    PMWP_M_WPDESKTOP,
    PMWP_WPDESKTOP,
    PMWP_M_WPDISK,
    PMWP_WPDISK,
    PMWP_M_WPPROGRAM,
    PMWP_WPPROGRAM,
    PMWP_M_WPSHADOW,
    PMWP_WPSHADOW,
    PMWP_WPCLASSMANAGER,
    PMWP_CLASS_COUNT
} PmwpClassId;

/* The SOM root class constructed before a class that derives from it directly. */
typedef enum PmwpRootClass
{
    PMWP_ROOT_NONE,
    PMWP_ROOT_OBJECT,
    PMWP_ROOT_CLASS,
    PMWP_ROOT_CLASS_MANAGER
} PmwpRootClass;

typedef struct PmwpMethodRow
{
    uint16 offset; /* Byte offset of the token in the class data structure. */
    const char *name;
    uint8 call_type; /* somMethodInfo.callType; nonzero list_size selects stub info. */
    uint8 list_size; /* somMethodInfo.va_listSize. */
} PmwpMethodRow;

typedef struct PmwpClassRow
{
    const char *name, *parent, *meta;
    PmwpClassId parent_id, meta_id;
    PmwpRootClass root;
    uint32 instance_size, max_methods, alignment, implicit_meta, class_data_entries;
    const PmwpMethodRow *methods;
    uint32 method_count;
    const char *const *overrides;
    uint32 override_count;
    const uint16 *procedures;
    uint32 procedure_count;
} PmwpClassRow;
extern const PmwpClassRow pmwpClassRows[PMWP_CLASS_COUNT];

/* A native method body. `method` is a simple name for both introduced methods and
   overrides; argument_bytes counts somSelf and every declared argument. */
typedef struct PmwpBody
{
    const char *class_name, *method;
    LxExecutionApi16 callback;
    uint16 argument_bytes;
} PmwpBody;
extern const PmwpBody pmwpObjectBodies[];
extern const size_t pmwpObjectBodyCount;
extern const PmwpBody pmwpViewBodies[];
extern const size_t pmwpViewBodyCount;
extern const PmwpBody pmwpClassBodies[];
extern const size_t pmwpClassBodyCount;

/* WPObject instance data. The use list, default view, style and concurrent view
   occupy their PMWP.DLL offsets; the remaining words are private to this provider. */
enum
{
    PMWP_OBJ_TITLE = 0x00,
    PMWP_OBJ_FOLDER = 0x08,
    PMWP_OBJ_HANDLE = 0x0c,
    PMWP_OBJ_IDENTIFIER = 0x10,
    PMWP_OBJ_LOCKS = 0x14,
    PMWP_OBJ_USE_LIST = 0x28,
    PMWP_OBJ_DEFAULT_VIEW = 0x5c,
    PMWP_OBJ_STYLE = 0x68,
    PMWP_OBJ_CONCURRENT_VIEW = 0x70,
    /* M_WPObject instance data, at PMWP.DLL offsets. */
    PMWP_CLS_USAGE = 0x04,
    PMWP_CLS_PAGE_WIDTH = 0x08,
    PMWP_CLS_PAGE_HEIGHT = 0x0c,
    PMWP_CLS_ERROR = 0x10
};

/* SOM class data offsets used through guest tokens. */
enum
{
    PMWP_SOMClass_somNew = 0x04,
    PMWP_SOMClass_somRenew = 0x08,
    PMWP_SOMClass_somClassReady = 0x10,
    PMWP_SOMClass_somGetName = 0x14,
    PMWP_SOMClass_somDescendedFrom = 0x1c,
    PMWP_SOMClass_somGetInstanceSize = 0x34,
    PMWP_SOMClass_somDeallocate = 0xc0,
    PMWP_SOMObject_somGetClass = 0x18,
    PMWP_SOMObject_somIsA = 0x1c
};

enum
{
    PMWP_USAGE_MEMORY = 1,
    PMWP_USAGE_OPENVIEW = 5,
    PMWP_USAGE_LAST = 0x16,
    PMWP_OPEN_DEFAULT = 0,
    PMWP_OPEN_SETTINGS = 2,
    PMWP_CCVIEW_ON = 1,
    PMWP_CCVIEW_OFF = 2,
    PMWP_OR_NEW = 1,
    PMWP_OR_AWAKE = 2,
    PMWP_OBJSTYLE_NOMOVE = 0x2,
    PMWP_OBJSTYLE_NOLINK = 0x4,
    PMWP_OBJSTYLE_NOCOPY = 0x8,
    PMWP_OBJSTYLE_NOTDEFAULTICON = 0x10,
    PMWP_OBJSTYLE_TEMPLATE = 0x20,
    PMWP_OBJSTYLE_NODELETE = 0x40,
    PMWP_OBJSTYLE_NOPRINT = 0x80,
    PMWP_OBJSTYLE_NODRAG = 0x100,
    PMWP_OBJSTYLE_NOTVISIBLE = 0x200,
    PMWP_OBJSTYLE_NOSETTINGS = 0x400,
    PMWP_OBJSTYLE_NORENAME = 0x800,
    PMWP_OBJSTYLE_NODROP = 0x1000,
    PMWP_OBJSTYLE_NODROPON = 0x2000,
    PMWP_OBJSTYLE_LOCKEDINPLACE = 0x20000
};

/* Guest SOM runtime. Every call below may run guest code, so callers hold no
   store or table lock across them. */
BOOL pmwpRuntimeReady(void);
uint32 pmwpClassNew(PmwpClassId id, uint32 major, uint32 minor);
uint32 pmwpClassObject(PmwpClassId id);
uint32 pmwpClassToken(PmwpClassId id, uint32 offset);
uint32 pmwpGuestCall(uint32 entry, uint32 a, uint32 b, uint32 c, uint32 d);
/* Dispatches through the method token, so the most derived override runs. */
uint32 pmwpInvoke(PmwpClassId id, uint32 offset, uint32 self, uint32 a, uint32 b, uint32 c);
uint32 pmwpInvokeClass(uint32 offset, uint32 self, uint32 a, uint32 b, uint32 c);
uint32 pmwpInvokeObject(uint32 offset, uint32 self, uint32 a, uint32 b, uint32 c);
uint32 pmwpInstanceData(PmwpClassId id, uint32 self);
/* The implementation inherited by `id` from its first parent. */
uint32 pmwpParentProc(PmwpClassId id, uint32 root_token_offset);
uint32 pmwpMalloc(uint32 bytes);
uint32 pmwpCalloc(uint32 bytes);
void pmwpFree(uint32 address);
uint32 pmwpGuestString(const char *text);
uint32 pmwpLoad32(uint32 address);
BOOL pmwpStore32(uint32 address, uint32 value);
uint32 pmwpObjectField(uint32 self, uint32 offset);
void pmwpSetObjectField(uint32 self, uint32 offset, uint32 value);
uint32 pmwpClassField(uint32 self, uint32 offset);
void pmwpSetClassField(uint32 self, uint32 offset, uint32 value);
/* A permanent guest string, shared by every caller naming the same text. */
uint32 pmwpConstantString(const char *text);

/* Object lifetime. */
uint32 pmwpNewObject(uint32 class_object, uint32 title, uint32 setup, uint32 folder, BOOL lock);
uint32 pmwpAwaken(ULONG handle);
uint32 pmwpAwakeObject(ULONG handle);
void pmwpRememberObject(ULONG handle, uint32 object);
void pmwpForgetObject(ULONG handle);
uint32 pmwpClassForName(const char *name);
uint32 pmwpFolderForLocation(const char *location);
ULONG pmwpCreateRegistered(const char *class_name, const char *module, const char *title,
                           const char *setup, const char *location, ULONG flags);

/* Persistent store, implemented in pmwp.c. */
typedef struct PmwpRecord
{
    char *data;
    char *class_name, *title, *location, *setup;
} PmwpRecord;
BOOL pmwpStoreLoad(ULONG handle, PmwpRecord *record);
void pmwpStoreRelease(PmwpRecord *record);
ULONG pmwpStoreCreate(const char *class_name, const char *title, const char *location,
                      const char *setup);
BOOL pmwpStoreSetTitle(ULONG handle, const char *title);
BOOL pmwpStoreSetLocation(ULONG handle, const char *location);
BOOL pmwpStoreAppendSetup(ULONG handle, const char *setup);
BOOL pmwpStoreDelete(ULONG handle);
ULONG pmwpStoreFind(const char *identifier);
ULONG *pmwpStoreList(size_t *count);
BOOL pmwpStoreIdentifier(ULONG handle, char *identifier, size_t capacity);
BOOL pmwpStorePath(ULONG handle, char *path, size_t capacity);
BOOL pmwpStoreSetValue(ULONG handle, const char *class_name, ULONG key, const void *data,
                       ULONG size);
BOOL pmwpStoreGetValue(ULONG handle, const char *class_name, ULONG key, void *data, ULONG *size);
BOOL pmwpStoreOpen(ULONG handle, ULONG view, BOOL activate, HWND *frame);
BOOL pmwpSetupValue(const char *setup, const char *key, char *value, size_t capacity);

/* Guest address of an exported class data structure, or zero when it is private. */
uint32 pmwpExportedClassData(PmwpClassId id);
#endif
