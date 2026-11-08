/* Public domain. No rights reserved. Wholly AI-authored. */
/* Binds the original guest SOM.DLL and builds the Workplace Shell classes with it.
   Static class information lives in guest memory, method bodies are generated
   32-bit bridges, and somBuildClass itself fills every class data token. */
#include "os2libs/pmwp/internal.h"
#include "os2libs/doscalls.h"
#include "guest/abi.h"
#include "hal/host_sync.h"
#include <stdio.h>

enum
{
    ARENA_BYTES = 0x40000,
    CODE_BYTES = 0x10000,
    SCI_BYTES = 164,
    SCI_LAYOUT = 4,
    CCDS_BYTES = 16,
    SOM_REDISPATCH_DEFAULT = 0xffffffffu,
    STUB_BYTES = 16,
    CONTROL_BYTES = 16,
    CONTROL_INIT_OFFSET = 0x1c,
    CONTROL_DESTRUCT_OFFSET = 0x2c,
    STUB_PROCEDURE_FLAG = 0x8000
};

/* Ordinals of the original SOM.DLL. */
enum
{
    SOM_CALLOC = 1,
    SOM_CLASS_DATA = 3,
    SOM_CLASS_MGR_DATA = 6,
    SOM_FREE = 10,
    SOM_MALLOC = 12,
    SOM_OBJECT_DATA = 14,
    SOM_CLASS_MGR_NEW = 20,
    SOM_CLASS_NEW = 21,
    SOM_OBJECT_NEW = 22,
    SOM_ID_FROM_STRING = 31,
    SOM_STRING_FROM_ID = 40,
    SOM_BUILD_CLASS = 49,
    SOM_PARENT_NUM_RESOLVE = 50
};

typedef struct ConstantString
{
    struct ConstantString *next;
    uint32 address;
    char text[];
} ConstantString;

static struct
{
    BOOL ready;
    LxGuestMapping *arena_mapping, *code_mapping;
    uint8 *arena;
    uint32 arena_base;
    size_t arena_used;
    uint32 calloc_cell, free_cell, malloc_cell;
    uint32 class_data, object_data, class_mgr_data;
    uint32 id_from_string, string_from_id, call_many;
    uint32 class_mgr_new, class_new, object_new, build_class, parent_resolve;
    uint32 unavailable_bridge;
    uint8 *code;
    uint32 code_base;
    size_t code_used;
    uint32 sci[PMWP_CLASS_COUNT], cds[PMWP_CLASS_COUNT], ccds[PMWP_CLASS_COUNT];
    const PmwpBody **bodies;
    uint32 *body_procs;
    size_t body_count;
} S;

static LxHostOnce runtime_once = LX_HOST_ONCE_INIT;
static LxHostMonitor *constant_lock;
static ConstantString *constants;
static int family_started;

uint32 pmwpGuestCall(uint32 entry, uint32 a, uint32 b, uint32 c, uint32 d)
{
    return entry ? lx_execution_callback32_4(GLoaderState.execution, entry, a, b, c, d) : 0;
}

uint32 pmwpLoad32(uint32 address)
{
    uint8 bytes[4];
    return address && lx_execution_read(GLoaderState.execution, address, bytes, sizeof bytes)
               ? lx_guest_load_u32(bytes)
               : 0;
}

BOOL pmwpStore32(uint32 address, uint32 value)
{
    uint8 bytes[4];
    lx_guest_store_u32(bytes, value);
    return address && lx_execution_write(GLoaderState.execution, address, bytes, sizeof bytes);
}

/* A guest allocation hook may be replaced by the application, so each cell is
   read at the time of use. */
uint32 pmwpMalloc(uint32 bytes)
{
    return pmwpGuestCall(pmwpLoad32(S.malloc_cell), bytes, 0, 0, 0);
}

uint32 pmwpCalloc(uint32 bytes)
{
    return pmwpGuestCall(pmwpLoad32(S.calloc_cell), 1, bytes, 0, 0);
}

void pmwpFree(uint32 address)
{
    if (address)
        pmwpGuestCall(pmwpLoad32(S.free_cell), address, 0, 0, 0);
}

uint32 pmwpGuestString(const char *text)
{
    size_t size = strlen(text) + 1;
    uint32 address = pmwpMalloc((uint32)size);
    if (address && !lx_execution_write(GLoaderState.execution, address, text, size))
    {
        pmwpFree(address);
        address = 0;
    }
    return address;
}

static uint32 arena_alloc(size_t bytes)
{
    size_t start = (S.arena_used + 3) & ~(size_t)3;
    if (bytes > ARENA_BYTES - start)
        return 0;
    S.arena_used = start + bytes;
    return S.arena_base + (uint32)start;
}

static void arena_put(uint32 address, uint32 value)
{
    lx_guest_store_u32(S.arena + (address - S.arena_base), value);
}

static uint32 arena_string(const char *text)
{
    size_t size = strlen(text) + 1;
    uint32 address = arena_alloc(size);
    if (address)
        memcpy(S.arena + (address - S.arena_base), text, size);
    return address;
}

/* A somId is the address of a cell holding the string pointer. SOM may rewrite the
   cell when it registers the identifier, so cells stay writable. */
static uint32 arena_id(const char *text)
{
    uint32 string = arena_string(text), cell = arena_alloc(4);
    if (!string || !cell)
        return 0;
    arena_put(cell, string);
    return cell;
}

/* Method tables hold somId * values: one more level of indirection. */
static uint32 arena_id_reference(const char *text)
{
    uint32 id = arena_id(text), cell = arena_alloc(4);
    if (!id || !cell)
        return 0;
    arena_put(cell, id);
    return cell;
}

uint32 pmwpConstantString(const char *text)
{
    lx_host_monitor_lock(constant_lock);
    for (ConstantString *entry = constants; entry; entry = entry->next)
        if (!strcmp(entry->text, text))
        {
            uint32 address = entry->address;
            lx_host_monitor_unlock(constant_lock);
            return address;
        }
    uint32 address = arena_string(text);
    ConstantString *entry = address ? malloc(sizeof *entry + strlen(text) + 1) : NULL;
    if (entry)
    {
        entry->address = address;
        strcpy(entry->text, text);
        entry->next = constants;
        constants = entry;
    }
    lx_host_monitor_unlock(constant_lock);
    return address;
}

/* Each method without a provider has a stub that pushes its table position and calls
   this bridge, so a reached method is named. It reports failure as zero. */
static uint32 unavailable_method(uint8 *arguments)
{
    /* The frame holds the stub's position word, the caller's return address, then somSelf. */
    uint32 position = lx_guest_load_u32(arguments), self = lx_guest_load_u32(arguments + 8);
    uint32 class_index = position >> 16 < PMWP_CLASS_COUNT ? position >> 16 : 0;
    const PmwpClassRow *row = &pmwpClassRows[class_index];
    uint32 index = position & 0xffff;
    if (index & STUB_PROCEDURE_FLAG)
    {
        fprintf(stderr, "2ine: PMWP %s class data procedure %u has no provider\n", row->name,
                index & ~(uint32)STUB_PROCEDURE_FLAG);
        return 0;
    }
    const char *name = index < row->method_count ? row->methods[index].name : "?";
    fprintf(stderr, "2ine: PMWP method %s::%s has no provider\n", row->name, name);
    if (class_index != PMWP_WPCLASSMANAGER)
    {
        if (row->name[0] == 'M')
            pmwpSetClassField(self, PMWP_CLS_ERROR, PMWP_ERROR_NO_PROVIDER);
        else
            pmwpSetObjectField(self, PMWP_OBJ_ERROR, PMWP_ERROR_NO_PROVIDER);
    }
    return pmwpMethodFailure(name);
}

/* Guest helper (entry, words, count): pushes count words last-first and calls entry,
   giving native code calls with more than four stack words. */
static const uint8 call_many_code[] = {
    0x55,                   /* push ebp */
    0x89, 0xe5,             /* mov ebp, esp */
    0x56,                   /* push esi */
    0x8b, 0x75, 0x0c,       /* mov esi, [ebp+12] */
    0x8b, 0x4d, 0x10,       /* mov ecx, [ebp+16] */
    0x85, 0xc9,             /* next: test ecx, ecx */
    0x74, 0x07,             /* jz call */
    0xff, 0x74, 0x8e, 0xfc, /* push dword [esi+ecx*4-4] */
    0x49,                   /* dec ecx */
    0xeb, 0xf5,             /* jmp next */
    0xff, 0x55, 0x08,       /* call: call [ebp+8] */
    0x8d, 0x65, 0xfc,       /* lea esp, [ebp-4] */
    0x5e,                   /* pop esi */
    0x5d,                   /* pop ebp */
    0xc3                    /* ret */
};

uint32 pmwpGuestCallMany(uint32 entry, const uint32 *words, uint32 count)
{
    uint32 block = pmwpMalloc(count * 4), result = 0;
    if (!block)
        return 0;
    for (uint32 i = 0; i < count; i++)
        pmwpStore32(block + i * 4, words[i]);
    result = pmwpGuestCall(S.call_many, entry, block, count, 0);
    pmwpFree(block);
    return result;
}

/* push position; call bridge; add esp, 4; ret. The caller's arguments stay in place. */
static uint32 emit_unavailable_stub(uint32 position)
{
    if (S.code_used + STUB_BYTES > CODE_BYTES)
        return 0;
    uint8 *stub = S.code + S.code_used;
    uint32 address = S.code_base + (uint32)S.code_used;
    stub[0] = 0x68;
    lx_guest_store_u32(stub + 1, position);
    stub[5] = 0xe8;
    lx_guest_store_u32(stub + 6, S.unavailable_bridge - (address + 10));
    stub[10] = 0x83;
    stub[11] = 0xc4;
    stub[12] = 0x04;
    stub[13] = 0xc3;
    S.code_used += STUB_BYTES;
    return address;
}

static uint32 body_proc(const char *class_name, const char *method);

static BOOL emit_bridges(void)
{
    const struct
    {
        const PmwpBody *rows;
        size_t count;
    } tables[] = {{pmwpObjectBodies, pmwpObjectBodyCount},
                  {pmwpViewBodies, pmwpViewBodyCount},
                  {pmwpClassBodies, pmwpClassBodyCount},
                  {pmwpExtendedBodies, pmwpExtendedBodyCount}};

    size_t total = 0;
    for (size_t i = 0; i < sizeof tables / sizeof *tables; i++)
        total += tables[i].count;
    S.bodies = calloc(total, sizeof *S.bodies);
    S.body_procs = calloc(total, sizeof *S.body_procs);
    S.code_mapping =
        lx_execution_map(GLoaderState.execution, 0, CODE_BYTES, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!S.bodies || !S.body_procs || !S.code_mapping)
        return FALSE;
    uint8 *code = lx_execution_mapping_host(S.code_mapping);
    uint32 base = lx_execution_mapping_address(S.code_mapping);
    size_t used = 0;
    const LxExecutionApi32 shared = {unavailable_method, 12, 0, LX_ABI_STACK32};
    size_t size = lx_execution_emit_bridge32(GLoaderState.execution, code, CODE_BYTES, &shared);
    if (!size)
        return FALSE;
    S.unavailable_bridge = base;
    used = size;
    for (size_t i = 0; i < sizeof tables / sizeof *tables; i++)
        for (size_t j = 0; j < tables[i].count; j++)
        {
            const PmwpBody *body = &tables[i].rows[j];
            const LxExecutionApi32 api = {body->callback, body->argument_bytes, 0, LX_ABI_STACK32};
            size = lx_execution_emit_bridge32(GLoaderState.execution, code + used,
                                              CODE_BYTES - used, &api);
            if (!size)
                return FALSE;
            S.bodies[S.body_count] = body;
            S.body_procs[S.body_count++] = base + (uint32)used;
            used += size;
        }
    if (used + sizeof call_many_code > CODE_BYTES)
        return FALSE;
    memcpy(code + used, call_many_code, sizeof call_many_code);
    S.call_many = base + (uint32)used;
    used += (sizeof call_many_code + 15) & ~(size_t)15;
    S.code = code;
    S.code_base = base;
    S.code_used = used;
    return TRUE;
}

static uint32 body_proc(const char *class_name, const char *method)
{
    for (size_t i = 0; i < S.body_count; i++)
        if (!strcmp(S.bodies[i]->class_name, class_name) && !strcmp(S.bodies[i]->method, method))
            return S.body_procs[i];
    return 0;
}

/* Field order follows somStaticClassInfo. An override whose body this provider does
   not supply is left out, so the class inherits its parent's implementation. */
static BOOL build_static_info(PmwpClassId id)
{
    const PmwpClassRow *row = &pmwpClassRows[id];
    uint32 cds = pmwpExportedClassData(id);
    if (!cds)
        cds = arena_alloc(4 * (1 + (size_t)row->class_data_entries));
    uint32 ccds = arena_alloc(CCDS_BYTES), sci = arena_alloc(SCI_BYTES);
    uint32 class_id = arena_id(row->name), parent_id = arena_id(row->parent);
    uint32 parents = arena_alloc(4), meta_id = row->meta ? arena_id(row->meta) : 0;
    uint32 smt = row->method_count ? arena_alloc((size_t)row->method_count * 24) : 0;
    uint32 omt = row->override_count ? arena_alloc((size_t)row->override_count * 8) : 0;
    uint32 pmt = row->procedure_count ? arena_alloc((size_t)row->procedure_count * 8) : 0;
    if (!cds || !ccds || !sci || !class_id || !parent_id || !parents || (row->meta && !meta_id) ||
        (row->method_count && !smt) || (row->override_count && !omt) ||
        (row->procedure_count && !pmt))
        return FALSE;
    memset(S.arena + (ccds - S.arena_base), 0, CCDS_BYTES);
    memset(S.arena + (sci - S.arena_base), 0, SCI_BYTES);
    arena_put(parents, parent_id);
    for (uint32 i = 0; i < row->method_count; i++)
    {
        const PmwpMethodRow *method = &row->methods[i];
        char descriptor[160];
        snprintf(descriptor, sizeof descriptor, "::%s::%s", row->name, method->name);
        uint32 name = arena_id_reference(method->name), full = arena_id_reference(descriptor);
        uint32 proc = body_proc(row->name, method->name), apply = 0;
        if (!proc)
            proc = emit_unavailable_stub((uint32)id << 16 | i);
        if (!name || !full || !proc)
            return FALSE;
        if (method->list_size)
        {
            /* somApRdInfo followed by the somMethodInfo it refers to. */
            apply = arena_alloc(24);
            if (!apply)
                return FALSE;
            arena_put(apply, 0);
            arena_put(apply + 4, 0);
            arena_put(apply + 8, apply + 12);
            arena_put(apply + 12, method->call_type);
            arena_put(apply + 16, method->list_size);
            arena_put(apply + 20, 0);
        }
        uint32 entry = smt + i * 24;
        arena_put(entry, cds + method->offset);
        arena_put(entry + 4, name);
        arena_put(entry + 8, full);
        arena_put(entry + 12, proc);
        arena_put(entry + 16, SOM_REDISPATCH_DEFAULT);
        arena_put(entry + 20, apply);
    }
    uint32 overrides = 0;
    for (uint32 i = 0; i < row->override_count; i++)
    {
        const char *name = row->overrides[i], *simple = strrchr(name, ':');
        uint32 proc = body_proc(row->name, simple ? simple + 1 : name);
        if (!proc)
            continue;
        uint32 id_reference = arena_id_reference(name);
        if (!id_reference)
            return FALSE;
        arena_put(omt + overrides * 8, id_reference);
        arena_put(omt + overrides * 8 + 4, proc);
        overrides++;
    }
    for (uint32 i = 0; i < row->procedure_count; i++)
    {
        uint32 proc = emit_unavailable_stub((uint32)id << 16 | STUB_PROCEDURE_FLAG | i);
        if (!proc)
            return FALSE;
        arena_put(pmt + i * 8, cds + row->procedures[i]);
        arena_put(pmt + i * 8 + 4, proc);
    }
    arena_put(sci + 0, SCI_LAYOUT);
    arena_put(sci + 4, row->method_count);
    arena_put(sci + 8, overrides);
    arena_put(sci + 16, row->procedure_count);
    arena_put(sci + 24, 1);
    arena_put(sci + 28, 2);
    arena_put(sci + 32, row->instance_size);
    arena_put(sci + 36, row->max_methods);
    arena_put(sci + 40, 1);
    arena_put(sci + 44, class_id);
    arena_put(sci + 48, meta_id);
    arena_put(sci + 52, row->implicit_meta);
    arena_put(sci + 56, parents);
    arena_put(sci + 60, cds);
    arena_put(sci + 64, ccds);
    arena_put(sci + 68, smt);
    arena_put(sci + 72, omt);
    arena_put(sci + 84, pmt);
    arena_put(sci + 96, row->alignment);
    arena_put(sci + 100, 0xffffffffu);
    arena_put(sci + 132, row->class_data_entries);
    S.sci[id] = sci;
    S.cds[id] = cds;
    S.ccds[id] = ccds;
    return TRUE;
}

static BOOL import_entry(HMODULE module, ULONG ordinal, uint32 *address)
{
    PFN entry = NULL;
    return DosQueryProcAddr(module, ordinal, NULL, &entry) == NO_ERROR && entry &&
           lx_execution_entry_address(GLoaderState.execution, (void *)entry, address);
}

static BOOL import_data(HMODULE module, ULONG ordinal, uint32 *address)
{
    PFN entry = NULL;
    return DosQueryProcAddr(module, ordinal, NULL, &entry) == NO_ERROR && entry &&
           lx_execution_address(GLoaderState.execution, (void *)entry, 1, LX_GUEST_READ, address);
}

/* The native som module exports nothing, so only the original SOM.DLL satisfies these
   imports. Without it every constructor reports failure. */
static BOOL bind_som(void)
{
    HMODULE module = 0;
    if (DosLoadModule(NULL, 0, "SOM", &module) != NO_ERROR)
        return FALSE;
    if (import_data(module, SOM_CALLOC, &S.calloc_cell) &&
        import_data(module, SOM_FREE, &S.free_cell) &&
        import_data(module, SOM_MALLOC, &S.malloc_cell) &&
        import_data(module, SOM_CLASS_DATA, &S.class_data) &&
        import_data(module, SOM_OBJECT_DATA, &S.object_data) &&
        import_data(module, SOM_CLASS_MGR_DATA, &S.class_mgr_data) &&
        import_entry(module, SOM_ID_FROM_STRING, &S.id_from_string) &&
        import_entry(module, SOM_STRING_FROM_ID, &S.string_from_id) &&
        import_entry(module, SOM_CLASS_MGR_NEW, &S.class_mgr_new) &&
        import_entry(module, SOM_CLASS_NEW, &S.class_new) &&
        import_entry(module, SOM_OBJECT_NEW, &S.object_new) &&
        import_entry(module, SOM_BUILD_CLASS, &S.build_class) &&
        import_entry(module, SOM_PARENT_NUM_RESOLVE, &S.parent_resolve))
        return TRUE;
    DosFreeModule(module);
    return FALSE;
}

static void initialize_runtime(void)
{
    constant_lock = lx_host_monitor_create();
    if (!constant_lock || !bind_som() || !emit_bridges())
        return;
    /* Class identifiers stay writable for SOM. */
    S.arena_mapping =
        lx_execution_map(GLoaderState.execution, 0, ARENA_BYTES, LX_GUEST_READ | LX_GUEST_WRITE, 0);
    if (!S.arena_mapping)
        return;
    S.arena = lx_execution_mapping_host(S.arena_mapping);
    S.arena_base = lx_execution_mapping_address(S.arena_mapping);
    for (int id = 0; id < PMWP_CLASS_COUNT; id++)
        if (!build_static_info((PmwpClassId)id))
            return;
    if (lx_execution_protect(GLoaderState.execution, S.code_mapping,
                             LX_GUEST_READ | LX_GUEST_EXECUTE))
        S.ready = TRUE;
}

BOOL pmwpRuntimeReady(void)
{
    lx_host_once(&runtime_once, initialize_runtime);
    return S.ready;
}

/* Parent and metaclass constructors run first, with the arguments PMWP.DLL passes.
   somBuildClass returns the existing class when one has already been built. */
static uint32 build_class(PmwpClassId id, uint32 major, uint32 minor)
{
    const PmwpClassRow *row = &pmwpClassRows[id];
    switch (row->root)
    {
        case PMWP_ROOT_OBJECT:
            pmwpGuestCall(S.object_new, 1, 4, 0, 0);
            break;
        case PMWP_ROOT_CLASS:
            pmwpGuestCall(S.class_new, 1, 4, 0, 0);
            break;
        case PMWP_ROOT_CLASS_MANAGER:
            pmwpGuestCall(S.class_mgr_new, 1, 4, 0, 0);
            break;
        default:
            break;
    }
    if (row->parent_id != PMWP_NO_CLASS && !build_class(row->parent_id, 1, 2))
        return 0;
    if (row->meta_id != PMWP_NO_CLASS && !build_class(row->meta_id, 1, 2))
        return 0;
    return pmwpGuestCall(S.build_class, 1, S.sci[id], major, minor);
}

uint32 pmwpClassNew(PmwpClassId id, uint32 major, uint32 minor)
{
    if (!pmwpRuntimeReady())
        return 0;
    uint32 result = build_class(id, major, minor);
    /* Guest code reads tokens of classes it never constructs, such as WPDisk and
       WPProgram, which the shell itself has already built on OS/2. */
    if (result && !__atomic_exchange_n(&family_started, 1, __ATOMIC_ACQ_REL))
        for (int other = 0; other < PMWP_CLASS_COUNT; other++)
            if (!build_class((PmwpClassId)other, 1, 2))
                TRACE_NATIVE("PMWP class %s could not be built", pmwpClassRows[other].name);
    return result;
}

uint32 pmwpClassObject(PmwpClassId id)
{
    return pmwpRuntimeReady() ? pmwpLoad32(S.cds[id]) : 0;
}

uint32 pmwpClassToken(PmwpClassId id, uint32 offset)
{
    return pmwpRuntimeReady() ? pmwpLoad32(S.cds[id] + offset) : 0;
}

uint32 pmwpInvoke(PmwpClassId id, uint32 offset, uint32 self, uint32 a, uint32 b, uint32 c)
{
    return pmwpGuestCall(pmwpClassToken(id, offset), self, a, b, c);
}

uint32 pmwpInvokeClass(uint32 offset, uint32 self, uint32 a, uint32 b, uint32 c)
{
    return pmwpGuestCall(pmwpLoad32(S.class_data + offset), self, a, b, c);
}

uint32 pmwpInvokeObject(uint32 offset, uint32 self, uint32 a, uint32 b, uint32 c)
{
    return pmwpGuestCall(pmwpLoad32(S.object_data + offset), self, a, b, c);
}

/* The instance data token at somCClassDataStructure +4 is callable like a method token. */
uint32 pmwpInstanceData(PmwpClassId id, uint32 self)
{
    return pmwpGuestCall(pmwpLoad32(S.ccds[id] + 4), self, 0, 0, 0);
}

uint32 pmwpParentProc(PmwpClassId id, uint32 root_token_offset)
{
    return pmwpGuestCall(S.parent_resolve, pmwpLoad32(S.ccds[id]), 1,
                         pmwpLoad32(S.class_data + root_token_offset), 0);
}

/* The implementation of a method inherited from the first parent of `id`. */
uint32 pmwpParentMethod(PmwpClassId id, uint32 token)
{
    return pmwpGuestCall(S.parent_resolve, pmwpLoad32(S.ccds[id]), 1, token, 0);
}

/* Generated initializers and destructors share this step. A null control selects the
   class's own control block from its parent method table structure. It advances the
   cursor and reports the parent procedure to run, or zero. */
static uint32 control_step(PmwpClassId id, uint32 *control, uint32 *owned, uint32 table_offset)
{
    *owned = 0;
    if (!*control)
    {
        uint32 table = pmwpLoad32(S.ccds[id]) + table_offset;
        *owned = pmwpMalloc(CONTROL_BYTES);
        if (!*owned)
            return 0;
        for (uint32 i = 0; i < CONTROL_BYTES; i += 4)
            pmwpStore32(*owned + i, pmwpLoad32(table + i));
        *control = *owned;
    }
    uint32 mask = pmwpLoad32(*control), info = pmwpLoad32(*control + 4);
    info += pmwpLoad32(*control + 8);
    pmwpStore32(*control, mask + 1);
    pmwpStore32(*control + 4, info);
    uint8 flags = 0;
    lx_execution_read(GLoaderState.execution, mask, &flags, 1);
    return flags & 1 ? pmwpLoad32(info + 4) : 0;
}

void pmwpGeneratedInit(PmwpClassId id, uint32 self, uint32 control)
{
    uint32 owned, parent = control_step(id, &control, &owned, CONTROL_INIT_OFFSET);
    if (parent)
        pmwpGuestCall(parent, self, control, 0, 0);
    pmwpFree(owned);
}

void pmwpGeneratedDestruct(PmwpClassId id, uint32 self, BOOL release, uint32 control)
{
    uint32 owned, parent = control_step(id, &control, &owned, CONTROL_DESTRUCT_OFFSET);
    if (parent)
        pmwpGuestCall(parent, self, 0, control, 0);
    pmwpFree(owned);
    if (release)
        pmwpInvokeClass(PMWP_SOMClass_somDeallocate, pmwpClassObject(id), self, 0, 0);
}

/* The class manager parent's implementation of a SOMClassMgr method. */
uint32 pmwpClassManagerParent(uint32 token_offset)
{
    return pmwpGuestCall(S.parent_resolve, pmwpLoad32(S.ccds[PMWP_WPCLASSMANAGER]), 1,
                         pmwpLoad32(S.class_mgr_data + token_offset), 0);
}

uint32 pmwpSomIdFromString(uint32 text)
{
    return pmwpGuestCall(S.id_from_string, text, 0, 0, 0);
}

uint32 pmwpSomStringFromId(uint32 id)
{
    return pmwpGuestCall(S.string_from_id, id, 0, 0, 0);
}

uint32 pmwpObjectField(uint32 self, uint32 offset)
{
    return pmwpLoad32(pmwpInstanceData(PMWP_WPOBJECT, self) + offset);
}

void pmwpSetObjectField(uint32 self, uint32 offset, uint32 value)
{
    pmwpStore32(pmwpInstanceData(PMWP_WPOBJECT, self) + offset, value);
}

uint32 pmwpClassField(uint32 self, uint32 offset)
{
    return pmwpLoad32(pmwpInstanceData(PMWP_M_WPOBJECT, self) + offset);
}

void pmwpSetClassField(uint32 self, uint32 offset, uint32 value)
{
    pmwpStore32(pmwpInstanceData(PMWP_M_WPOBJECT, self) + offset, value);
}
